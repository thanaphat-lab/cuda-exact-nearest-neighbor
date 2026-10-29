#pragma once

#include "s6/cpu_baseline.hpp"

#include <cstdint>

namespace s6::detail {

class CandidateVisitObserver {
  public:
    virtual ~CandidateVisitObserver() = default;
    virtual void observe(std::uint64_t index) noexcept = 0;
    virtual void finish(bool completed) noexcept = 0;
};

ProductionOutcome cpu_search_float32_impl(DatasetView dataset, QueryView query,
                                          CandidateVisitObserver* observer);

}  // namespace s6::detail
