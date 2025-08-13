#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <cstdint>

class PDFRenderer;

// Task describing a page render at a specific pixel size for a generation
struct PageRenderTask {
    int pageIndex{0};
    int pixelWidth{0};
    int pixelHeight{0};
    int generation{0};
    int priority{0}; // smaller = higher priority
    bool preview{false}; // preview quality (gesture in progress) = cheaper render
};

// Result containing BGRA pixels for upload on the GL thread
struct PageRenderResult {
    int pageIndex{0};
    int width{0};
    int height{0};
    int generation{0};
    std::vector<std::uint8_t> bgra; // width*height*4
    bool preview{false}; // propagate preview flag for GL upload filtering (e.g., skip mipmaps)
};

// Simple async queue with a single worker thread for PDFium rendering.
// GL uploads must be done on the UI/GL thread by polling drainResults().
class AsyncRenderQueue {
public:
    explicit AsyncRenderQueue(PDFRenderer* renderer);
    ~AsyncRenderQueue();

    // Replace pending jobs with the provided list for the given generation.
    // Older generations are ignored/cancelled.
    void submit(std::vector<PageRenderTask> tasks, int generation);

    // Move all ready results out; typically called from the update() / UI thread.
    std::vector<PageRenderResult> drainResults();

    // Cancel all work and bump generation to avoid stale uploads
    void cancelAll();

private:
    void workerLoop();

    PDFRenderer* m_renderer; // non-owning

    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop{false};

    // Min-heap simulated via priority sort when submitting; simple queue here.
    std::queue<PageRenderTask> m_tasks;
    std::mutex m_resultsMutex;
    std::vector<PageRenderResult> m_results;

    std::atomic<int> m_currentGeneration{0};
};
