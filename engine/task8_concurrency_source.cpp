#include <obs-module.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

OBS_DECLARE_MODULE()

namespace {

struct TestSource {
    obs_source_t *source = nullptr;
    std::string label;
    std::string last_scenario;
    uint32_t width = 320;
    uint32_t height = 180;
    std::mutex workers_mutex;
    std::vector<std::thread> workers;
};

std::mutex g_state_mutex;
std::condition_variable g_state_cv;
std::unordered_map<std::string, TestSource *> g_sources;
uint64_t g_destroy_epoch = 0;
uint64_t g_destroy_completed_epoch = 0;
bool g_destroy_worker_armed = false;

uint32_t read_dimension(obs_data_t *settings, const char *name, uint32_t fallback)
{
    const long long value = obs_data_get_int(settings, name);
    return value > 0 ? static_cast<uint32_t>(value) : fallback;
}

obs_source_t *retain_source(const std::string &label)
{
    std::lock_guard lock(g_state_mutex);
    const auto it = g_sources.find(label);
    if (it == g_sources.end() || !it->second || !it->second->source)
        return nullptr;
    return obs_source_get_ref(it->second->source);
}

void update_retained_source(obs_source_t *source, long long marker)
{
    if (!source)
        return;
    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings)
        return;
    obs_data_set_int(settings, "marker", marker);
    obs_source_update(source, settings);
    obs_data_release(settings);
}

void update_peer(const std::string &label, long long marker)
{
    obs_source_t *source = retain_source(label);
    if (!source)
        return;
    update_retained_source(source, marker);
    obs_source_release(source);
}

void add_worker(TestSource *context, std::thread worker)
{
    std::lock_guard lock(context->workers_mutex);
    context->workers.push_back(std::move(worker));
}

void join_workers(TestSource *context)
{
    std::vector<std::thread> workers;
    {
        std::lock_guard lock(context->workers_mutex);
        workers.swap(context->workers);
    }
    for (std::thread &worker : workers) {
        if (worker.joinable())
            worker.join();
    }
}

void run_peer_scenario(const std::string &scenario)
{
    std::thread worker([scenario] {
        if (scenario == "B") {
            update_peer("B", 1);
            return;
        }
        if (scenario == "BC") {
            update_peer("B", 1);
            update_peer("C", 1);
            return;
        }
        if (scenario == "OVERFLOW") {
            obs_source_t *peer = retain_source("B");
            if (!peer)
                return;
            for (long long marker = 1; marker <= 1030; ++marker)
                update_retained_source(peer, marker);
            obs_source_release(peer);
        }
    });
    worker.join();
}

void arm_destroy_worker(TestSource *context)
{
    obs_source_t *self = obs_source_get_ref(context->source);
    uint64_t observed_epoch = 0;
    {
        std::lock_guard lock(g_state_mutex);
        observed_epoch = g_destroy_epoch;
        g_destroy_worker_armed = true;
    }

    add_worker(context, std::thread([self, observed_epoch] {
        uint64_t target_epoch = 0;
        {
            std::unique_lock lock(g_state_mutex);
            g_state_cv.wait(lock, [&] { return g_destroy_epoch > observed_epoch; });
            target_epoch = g_destroy_epoch;
        }

        update_retained_source(self, 1);
        obs_source_release(self);

        {
            std::lock_guard lock(g_state_mutex);
            g_destroy_completed_epoch = std::max(g_destroy_completed_epoch, target_epoch);
        }
        g_state_cv.notify_all();
    }));
}

void arm_delayed_worker(TestSource *context, uint32_t delay_ms)
{
    obs_source_t *self = obs_source_get_ref(context->source);
    add_worker(context, std::thread([self, delay_ms] {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        update_retained_source(self, 1);
        obs_source_release(self);
    }));
}

const char *video_get_name(void *)
{
    return "Task 8 Concurrency Video";
}

const char *peer_get_name(void *)
{
    return "Task 8 Concurrency Peer";
}

void source_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "label", "");
    obs_data_set_default_string(settings, "scenario", "");
    obs_data_set_default_int(settings, "width", 320);
    obs_data_set_default_int(settings, "height", 180);
    obs_data_set_default_int(settings, "marker", 0);
    obs_data_set_default_int(settings, "delayMs", 250);
}

void *source_create(obs_data_t *settings, obs_source_t *source)
{
    auto *context = new TestSource();
    context->source = source;
    const char *label = obs_data_get_string(settings, "label");
    context->label = label && *label ? label : obs_source_get_name(source);
    context->width = read_dimension(settings, "width", 320);
    context->height = read_dimension(settings, "height", 180);

    {
        std::lock_guard lock(g_state_mutex);
        g_sources[context->label] = context;
    }
    return context;
}

void source_destroy(void *data)
{
    auto *context = static_cast<TestSource *>(data);
    if (!context)
        return;

    if (context->label == "A") {
        std::unique_lock lock(g_state_mutex);
        if (g_destroy_worker_armed) {
            const uint64_t target_epoch = ++g_destroy_epoch;
            g_state_cv.notify_all();
            const bool completed = g_state_cv.wait_for(lock, std::chrono::seconds(5), [&] {
                return g_destroy_completed_epoch >= target_epoch;
            });
            if (!completed)
                blog(LOG_ERROR, "[task8-concurrency] timed out waiting for destroy-triggered peer update");
        }
    }

    {
        std::lock_guard lock(g_state_mutex);
        const auto it = g_sources.find(context->label);
        if (it != g_sources.end() && it->second == context)
            g_sources.erase(it);
    }

    join_workers(context);
    delete context;
}

void source_update(void *data, obs_data_t *settings)
{
    auto *context = static_cast<TestSource *>(data);
    if (!context)
        return;

    context->width = read_dimension(settings, "width", context->width);
    context->height = read_dimension(settings, "height", context->height);

    const char *scenario_text = obs_data_get_string(settings, "scenario");
    const std::string scenario = scenario_text ? scenario_text : "";
    if (scenario.empty() || scenario == context->last_scenario)
        return;
    context->last_scenario = scenario;

    if (context->label == "A") {
        if (scenario == "B" || scenario == "BC" || scenario == "OVERFLOW")
            run_peer_scenario(scenario);
        return;
    }

    if (context->label == "B" && scenario == "ARM_DESTROY") {
        arm_destroy_worker(context);
        return;
    }

    if (context->label == "B" && scenario == "DELAYED") {
        const long long requested_delay = obs_data_get_int(settings, "delayMs");
        const uint32_t delay_ms = requested_delay > 0 ? static_cast<uint32_t>(requested_delay) : 250;
        arm_delayed_worker(context, delay_ms);
    }
}

uint32_t source_width(void *data)
{
    return static_cast<TestSource *>(data)->width;
}

uint32_t source_height(void *data)
{
    return static_cast<TestSource *>(data)->height;
}

obs_source_info g_video_info = {};
obs_source_info g_peer_info = {};

} // namespace

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Task 8 deterministic source concurrency test module";
}

bool obs_module_load(void)
{
    g_video_info.id = "task8_concurrency_video";
    g_video_info.type = OBS_SOURCE_TYPE_INPUT;
    g_video_info.output_flags = OBS_SOURCE_VIDEO;
    g_video_info.get_name = video_get_name;
    g_video_info.create = source_create;
    g_video_info.destroy = source_destroy;
    g_video_info.update = source_update;
    g_video_info.get_defaults = source_defaults;
    g_video_info.get_width = source_width;
    g_video_info.get_height = source_height;

    g_peer_info.id = "task8_concurrency_peer";
    g_peer_info.type = OBS_SOURCE_TYPE_INPUT;
    g_peer_info.output_flags = 0;
    g_peer_info.get_name = peer_get_name;
    g_peer_info.create = source_create;
    g_peer_info.destroy = source_destroy;
    g_peer_info.update = source_update;
    g_peer_info.get_defaults = source_defaults;
    g_peer_info.get_width = source_width;
    g_peer_info.get_height = source_height;

    obs_register_source(&g_video_info);
    obs_register_source(&g_peer_info);
    blog(LOG_INFO, "[task8-concurrency] deterministic test module loaded");
    return true;
}
