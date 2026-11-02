#pragma once

#include "s6/benchmark_internal.hpp"

#include <cuda_runtime.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace s6::detail::benchmark_internal {

using Clock = std::chrono::steady_clock;

inline std::uint64_t elapsed_ns(Clock::time_point start,
                                Clock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

class StageEvents {
  public:
    explicit StageEvents(S7CudaStageTiming* output) noexcept : output_(output) {
        if (output_ == nullptr) {
            return;
        }
        cudaError_t error = cudaSuccess;
        for (auto& event : events_) {
            error = cudaEventCreateWithFlags(&event, cudaEventDefault);
            if (error != cudaSuccess) {
                output_->timing_detail =
                    std::string("cudaEventCreateWithFlags: ") +
                    cudaGetErrorString(error);
                return;
            }
        }
        ready_ = true;
    }

    StageEvents(const StageEvents&) = delete;
    StageEvents& operator=(const StageEvents&) = delete;

    ~StageEvents() {
        for (auto event : events_) {
            if (event != nullptr) {
                (void)cudaEventDestroy(event);
            }
        }
    }

    bool ready() const noexcept { return ready_; }

    void record(unsigned index, cudaStream_t stream) noexcept {
        if (!ready_ || index >= 5U) {
            return;
        }
        const auto error = cudaEventRecord(events_[index], stream);
        if (error != cudaSuccess) {
            ready_ = false;
            output_->timing_detail = std::string("cudaEventRecord: ") +
                                     cudaGetErrorString(error);
        }
    }

    void finish() noexcept {
        if (!ready_) {
            return;
        }
        float h2d_ms = 0.0F;
        float reset_ms = 0.0F;
        float kernel_ms = 0.0F;
        float d2h_ms = 0.0F;
        cudaError_t error = cudaEventElapsedTime(&h2d_ms, events_[0], events_[1]);
        if (error == cudaSuccess) {
            error = cudaEventElapsedTime(&reset_ms, events_[1], events_[2]);
        }
        if (error == cudaSuccess) {
            error = cudaEventElapsedTime(&kernel_ms, events_[2], events_[3]);
        }
        if (error == cudaSuccess) {
            error = cudaEventElapsedTime(&d2h_ms, events_[3], events_[4]);
        }
        if (error != cudaSuccess) {
            output_->timing_detail = std::string("cudaEventElapsedTime: ") +
                                     cudaGetErrorString(error);
            return;
        }
        output_->h2d_ns = milliseconds_to_ns(h2d_ms);
        output_->workspace_reset_ns = milliseconds_to_ns(reset_ms);
        output_->kernel_ns = milliseconds_to_ns(kernel_ms);
        output_->d2h_ns = milliseconds_to_ns(d2h_ms);
        output_->device_timing_available = true;
        output_->timing_detail = "CUDA events recorded on the production stream";
    }

  private:
    static std::uint64_t milliseconds_to_ns(float value) noexcept {
        const double ns = static_cast<double>(value) * 1000000.0;
        return ns <= 0.0 ? 0U : static_cast<std::uint64_t>(ns + 0.5);
    }

    S7CudaStageTiming* output_ = nullptr;
    cudaEvent_t events_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    bool ready_ = false;
};

}  // namespace s6::detail::benchmark_internal
