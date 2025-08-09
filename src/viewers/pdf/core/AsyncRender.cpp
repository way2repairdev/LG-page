#include "viewers/pdf/AsyncRender.h"
#include "rendering/pdf-render.h"
#include "fpdfview.h"

#include <algorithm>
#include <cstring>

AsyncRenderQueue::AsyncRenderQueue(PDFRenderer* renderer)
    : m_renderer(renderer) {
    m_worker = std::thread(&AsyncRenderQueue::workerLoop, this);
}

AsyncRenderQueue::~AsyncRenderQueue() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

void AsyncRenderQueue::submit(std::vector<PageRenderTask> tasks, int generation) {
    // Sort by priority ascending so earlier tasks are processed first
    std::sort(tasks.begin(), tasks.end(), [](const PageRenderTask& a, const PageRenderTask& b){
        if (a.priority == b.priority) return a.pageIndex < b.pageIndex;
        return a.priority < b.priority;
    });

    std::lock_guard<std::mutex> lk(m_mutex);
    // Reset queue with new generation
    m_currentGeneration.store(generation);
    std::queue<PageRenderTask> empty;
    std::swap(m_tasks, empty);
    for (auto& t : tasks) m_tasks.push(t);
    m_cv.notify_all();
}

std::vector<PageRenderResult> AsyncRenderQueue::drainResults() {
    std::lock_guard<std::mutex> lk(m_resultsMutex);
    std::vector<PageRenderResult> out;
    out.swap(m_results);
    return out;
}

void AsyncRenderQueue::cancelAll() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_currentGeneration.fetch_add(1);
    std::queue<PageRenderTask> empty;
    std::swap(m_tasks, empty);
}

void AsyncRenderQueue::workerLoop() {
    while (true) {
        PageRenderTask task;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [&]{ return m_stop || !m_tasks.empty(); });
            if (m_stop) return;
            task = m_tasks.front();
            m_tasks.pop();
        }

        // Drop stale tasks
        if (task.generation != m_currentGeneration.load()) {
            continue;
        }

        // Render using PDFRenderer API to a bitmap at requested size
        if (!m_renderer) continue;
        FPDF_BITMAP bmp = m_renderer->RenderPageToBitmap(task.pageIndex, task.pixelWidth, task.pixelHeight);
        if (!bmp) continue;

        int stride = FPDFBitmap_GetStride(bmp);
        void* buffer = FPDFBitmap_GetBuffer(bmp);
        if (!buffer) {
            FPDFBitmap_Destroy(bmp);
            continue;
        }

        PageRenderResult res;
        res.pageIndex = task.pageIndex;
        res.width = task.pixelWidth;
        res.height = task.pixelHeight;
        res.generation = task.generation;
        res.bgra.resize(res.height * stride);
        std::memcpy(res.bgra.data(), buffer, res.bgra.size());

        FPDFBitmap_Destroy(bmp);

        // Store result
        {
            std::lock_guard<std::mutex> lk(m_resultsMutex);
            m_results.emplace_back(std::move(res));
        }
    }
}
