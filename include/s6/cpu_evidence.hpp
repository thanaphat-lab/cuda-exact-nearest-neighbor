#pragma once

#include "s6/cpu_baseline.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s6 {

inline constexpr const char* kCpuEvidenceSchema =
    "S6-CPU-PRODUCTION-EVIDENCE-v1.0";

struct CoverageAssessment {
    EvidenceStatus status = EvidenceStatus::blocked;
    std::uint64_t expected_count = 0;
    std::uint64_t observed_unique_count = 0;
    std::uint64_t duplicate_count = 0;
    std::uint64_t out_of_range_count = 0;
    std::uint64_t missing_count = 0;
    bool completed = false;
};

CoverageAssessment assess_candidate_visitation(
    std::uint64_t expected_count, const std::vector<std::uint64_t>& visits,
    bool completed);

struct GraphInspectionAssessment {
    EvidenceStatus status = EvidenceStatus::blocked;
    std::string detail;
};

GraphInspectionAssessment assess_graph_disassembly_for_testing(
    const std::string& object_disassembly,
    const std::string& executable_disassembly,
    const std::string& linker_map);

class CpuBuildEvidence {
  public:
    EvidenceStatus identity_status() const noexcept { return identity_status_; }
    EvidenceStatus graph_status() const noexcept { return graph_status_; }
    bool graph_defect_demonstrated() const noexcept {
        return graph_defect_demonstrated_;
    }
    const std::string& identity() const noexcept { return identity_; }
    const std::string& detail() const noexcept { return detail_; }

  private:
    EvidenceStatus identity_status_ = EvidenceStatus::blocked;
    EvidenceStatus graph_status_ = EvidenceStatus::blocked;
    bool graph_defect_demonstrated_ = false;
    std::string identity_;
    std::string detail_;

    friend CpuBuildEvidence verify_cpu_build_evidence(const std::string& manifest_path);
};

CpuBuildEvidence verify_cpu_build_evidence(const std::string& manifest_path);

struct EvidencedProductionOutcome;

class CpuProductionEvidence {
  public:
    EvidenceStatus identity_status() const noexcept { return identity_status_; }
    EvidenceStatus graph_status() const noexcept { return graph_status_; }
    EvidenceStatus coverage_status() const noexcept { return coverage_.status; }
    bool graph_defect_demonstrated() const noexcept {
        return graph_defect_demonstrated_;
    }
    const CoverageAssessment& coverage() const noexcept { return coverage_; }
    const std::string& identity() const noexcept { return identity_; }
    const std::string& workload_identity() const noexcept {
        return workload_identity_;
    }
    const std::string& detail() const noexcept { return detail_; }

    bool matches(DatasetView dataset, QueryView query,
                 const ProductionOutcome& production) const;

  private:
    EvidenceStatus identity_status_ = EvidenceStatus::blocked;
    EvidenceStatus graph_status_ = EvidenceStatus::blocked;
    bool graph_defect_demonstrated_ = false;
    CoverageAssessment coverage_;
    std::string identity_;
    std::string workload_identity_;
    std::string detail_;
    InvocationStatus invocation_status_ = InvocationStatus::not_executed;
    bool has_result_ = false;
    std::uint64_t result_index_ = 0;
    std::uint32_t result_distance_bits_ = 0;

    friend struct EvidencedProductionOutcome;
    friend EvidencedProductionOutcome cpu_search_float32_evidenced(
        DatasetView dataset, QueryView query, const CpuBuildEvidence& build_evidence);
};

struct EvidencedProductionOutcome {
    ProductionOutcome production;
    CpuProductionEvidence evidence;
};

EvidencedProductionOutcome cpu_search_float32_evidenced(
    DatasetView dataset, QueryView query, const CpuBuildEvidence& build_evidence);

}  // namespace s6
