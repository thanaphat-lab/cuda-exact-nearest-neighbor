#pragma once

#include "s6/cpu_baseline.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s6 {

inline constexpr const char* kCpuEvidenceSchema =
    "S6-CPU-PRODUCTION-EVIDENCE-v1.2";
inline constexpr const char* kCpuFixtureEvidenceSchema =
    "S6-CPU-FIXTURE-EVIDENCE-v1.0";
inline constexpr const char* kCpuMatrixEvidenceSchema =
    "S6-CPU-MATRIX-EVIDENCE-v1.0";

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
    EvidenceStatus production_float32_status = EvidenceStatus::blocked;
    EvidenceStatus validator_float32_status = EvidenceStatus::blocked;
    EvidenceStatus reference_float64_status = EvidenceStatus::blocked;
    EvidenceStatus validator_float64_status = EvidenceStatus::blocked;
    std::string production_float32_detail;
    std::string validator_float32_detail;
    std::string reference_float64_detail;
    std::string validator_float64_detail;
};

GraphInspectionAssessment assess_graph_disassembly_for_testing(
    const std::string& object_disassembly,
    const std::string& executable_disassembly,
    const std::string& linker_map);

class CpuBuildEvidence {
  public:
    EvidenceStatus identity_status() const noexcept { return identity_status_; }
    EvidenceStatus graph_status() const noexcept { return graph_status_; }
    EvidenceStatus production_float32_graph_status() const noexcept {
        return production_float32_graph_status_;
    }
    EvidenceStatus validator_float32_graph_status() const noexcept {
        return validator_float32_graph_status_;
    }
    EvidenceStatus reference_float64_graph_status() const noexcept {
        return reference_float64_graph_status_;
    }
    EvidenceStatus validator_float64_graph_status() const noexcept {
        return validator_float64_graph_status_;
    }
    bool graph_defect_demonstrated() const noexcept {
        return graph_defect_demonstrated_;
    }
    const std::string& identity() const noexcept { return identity_; }
    const std::string& detail() const noexcept { return detail_; }
    const std::string& source_identity() const noexcept { return source_identity_; }
    const std::string& revision() const noexcept { return revision_; }
    const std::string& working_tree_state() const noexcept { return working_tree_state_; }
    const std::string& working_tree_fingerprint() const noexcept {
        return working_tree_fingerprint_;
    }
    const std::string& source_manifest_sha256() const noexcept {
        return source_manifest_sha256_;
    }
    const std::string& compiler_id() const noexcept { return compiler_id_; }
    const std::string& compiler_version() const noexcept { return compiler_version_; }
    const std::string& required_compile_flags() const noexcept {
        return required_compile_flags_;
    }

  private:
    EvidenceStatus identity_status_ = EvidenceStatus::blocked;
    EvidenceStatus graph_status_ = EvidenceStatus::blocked;
    EvidenceStatus production_float32_graph_status_ = EvidenceStatus::blocked;
    EvidenceStatus validator_float32_graph_status_ = EvidenceStatus::blocked;
    EvidenceStatus reference_float64_graph_status_ = EvidenceStatus::blocked;
    EvidenceStatus validator_float64_graph_status_ = EvidenceStatus::blocked;
    bool graph_defect_demonstrated_ = false;
    std::string identity_;
    std::string detail_;
    std::string source_identity_;
    std::string revision_;
    std::string working_tree_state_;
    std::string working_tree_fingerprint_;
    std::string source_manifest_sha256_;
    std::string compiler_id_;
    std::string compiler_version_;
    std::string required_compile_flags_;

    friend CpuBuildEvidence verify_cpu_build_evidence(const std::string& manifest_path);
};

CpuBuildEvidence verify_cpu_build_evidence(const std::string& manifest_path);

struct EvidencedProductionOutcome;

class CpuProductionEvidence {
  public:
    EvidenceStatus identity_status() const noexcept { return identity_status_; }
    EvidenceStatus graph_status() const noexcept { return graph_status_; }
    EvidenceStatus production_float32_graph_status() const noexcept {
        return production_float32_graph_status_;
    }
    EvidenceStatus validator_float32_graph_status() const noexcept {
        return validator_float32_graph_status_;
    }
    EvidenceStatus reference_float64_graph_status() const noexcept {
        return reference_float64_graph_status_;
    }
    EvidenceStatus validator_float64_graph_status() const noexcept {
        return validator_float64_graph_status_;
    }
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
    const std::string& source_identity() const noexcept { return source_identity_; }

    bool matches(DatasetView dataset, QueryView query,
                 const ProductionOutcome& production) const;

  private:
    EvidenceStatus identity_status_ = EvidenceStatus::blocked;
    EvidenceStatus graph_status_ = EvidenceStatus::blocked;
    EvidenceStatus production_float32_graph_status_ = EvidenceStatus::blocked;
    EvidenceStatus validator_float32_graph_status_ = EvidenceStatus::blocked;
    EvidenceStatus reference_float64_graph_status_ = EvidenceStatus::blocked;
    EvidenceStatus validator_float64_graph_status_ = EvidenceStatus::blocked;
    bool graph_defect_demonstrated_ = false;
    CoverageAssessment coverage_;
    std::string identity_;
    std::string workload_identity_;
    std::string detail_;
    std::string source_identity_;
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

bool write_cpu_fixture_evidence(const std::string& path,
                                const CpuBuildEvidence& build_evidence,
                                const MandatoryGateEvidence& gates,
                                std::string& detail);
MandatoryGateEvidence load_cpu_fixture_evidence(
    const std::string& path, const std::string& expected_source_identity,
    std::string& detail);

struct MatrixReportSummary {
    EvidenceStatus status = EvidenceStatus::blocked;
    std::uint64_t candidate_count = 0;
    std::uint64_t candidate_bytes = 0;
    std::string candidate_sha256;
    std::string metadata_path;
    std::string candidate_path;
    std::string detail;
};

MatrixReportSummary write_cpu_matrix_case_report(
    const std::string& report_root, const std::string& case_id,
    std::uint64_t seed, DatasetView dataset, QueryView query,
    const CpuBuildEvidence& build_evidence,
    const EvidencedProductionOutcome& evidenced,
    const Float64Reference& reference, const ValidationReport& report,
    const MandatoryGateEvidence& gates,
    const std::vector<std::uint64_t>& mathematical_distance_numerators,
    std::int32_t mathematical_distance_exponent);
MatrixReportSummary verify_cpu_matrix_case_report(
    const std::string& metadata_path);

}  // namespace s6
