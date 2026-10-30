#include "s6/cpu_baseline.hpp"
#include "s6/cpu_evidence.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <array>
#include <cctype>
#include <iostream>
#include <limits>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read test artifact: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write test artifact");
    }
    output << text;
}

std::vector<char> read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read binary test artifact");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_binary(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write binary test artifact");
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::uint64_t double_bits(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::string field(const std::string& manifest, const std::string& name) {
    const auto prefix = name + "=";
    const auto start = manifest.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("manifest test field missing");
    }
    const auto value_start = start + prefix.size();
    const auto end = manifest.find('\n', value_start);
    auto value = manifest.substr(value_start, end - value_start);
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return value;
}

std::string replace_field(std::string manifest, const std::string& name,
                          const std::string& replacement) {
    const auto prefix = name + "=";
    const auto start = manifest.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("manifest test field missing");
    }
    const auto value_start = start + prefix.size();
    const auto end = manifest.find('\n', value_start);
    manifest.replace(value_start, end - value_start, replacement);
    return manifest;
}

std::string remove_field(std::string manifest, const std::string& name) {
    const auto prefix = name + "=";
    const auto start = manifest.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("manifest test field missing");
    }
    const auto end = manifest.find('\n', start);
    manifest.erase(start, end == std::string::npos ? std::string::npos
                                                    : end - start + 1U);
    return manifest;
}

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot hash negative-test artifact");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
            "negative-test SHA provider failed");
    DWORD copied = 0;
    DWORD object_size = 0;
    require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                              &copied, 0) >= 0,
            "negative-test SHA object size failed");
    std::vector<unsigned char> object(object_size);
    require(BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0,
            "negative-test SHA creation failed");
    std::array<char, 4096> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            require(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                                   static_cast<ULONG>(count), 0) >= 0,
                    "negative-test SHA update failed");
        }
    }
    std::array<unsigned char, 32> digest{};
    require(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0,
            "negative-test SHA finish failed");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

void require_rejected_manifest(const std::filesystem::path& path, const char* message) {
    const auto rejected = s6::verify_cpu_build_evidence(path.string());
    require(rejected.identity_status() == s6::EvidenceStatus::failed &&
                rejected.graph_status() == s6::EvidenceStatus::blocked,
            message);
}

void test_manifest_rejection(const std::filesystem::path& manifest_path) {
    const auto original = read_text(manifest_path);
    const auto directory = manifest_path.parent_path() / "negative-evidence";
    std::filesystem::create_directories(directory);

    const std::vector<std::pair<std::string, std::string>> corruptions = {
        {"revision", std::string(40, '0')},
        {"working_tree_state", "dirty"},
        {"source_manifest_sha256", std::string(64, '0')},
        {"compiler_id", "invalid"},
        {"compiler_version", "0.0.invalid"},
        {"build_configuration", "Debug"},
        {"required_compile_flags", "/fp:fast"},
        {"executable_sha256", std::string(64, '0')},
        {"baseline_source_sha256", std::string(64, 'f')}};
    std::size_t case_index = 0;
    for (const auto& corruption : corruptions) {
        const auto path = directory / ("manifest-" + std::to_string(case_index++) + ".txt");
        write_text(path, replace_field(original, corruption.first, corruption.second));
        require_rejected_manifest(path, "mismatched build evidence was not rejected");
    }

    const auto source_root =
        std::filesystem::path(field(original, "baseline_source_path")).parent_path().parent_path();
    const auto readme = source_root / "README.md";
    const auto readme_hash = sha256_file(readme);
    auto forged = replace_field(original, "object_path", readme.string());
    forged = replace_field(forged, "object_sha256", readme_hash);
    forged = replace_field(forged, "library_path", readme.string());
    forged = replace_field(forged, "library_sha256", readme_hash);
    const auto forged_path = directory / "forged-readme.manifest.txt";
    write_text(forged_path, forged);
    require_rejected_manifest(forged_path,
                              "caller-controlled README object/library bypass was accepted");

    auto swapped = replace_field(original, "object_path", field(original, "library_path"));
    swapped = replace_field(swapped, "object_sha256", field(original, "library_sha256"));
    swapped = replace_field(swapped, "library_path", field(original, "object_path"));
    swapped = replace_field(swapped, "library_sha256", field(original, "object_sha256"));
    const auto swapped_path = directory / "role-substitution.manifest.txt";
    write_text(swapped_path, swapped);
    require_rejected_manifest(swapped_path, "object/library role substitution was accepted");

    auto malformed = original;
    const auto object_field = std::string("object_path=");
    const auto object_start = malformed.find(object_field);
    require(object_start != std::string::npos, "malformed manifest anchor missing");
    malformed.erase(object_start, malformed.find('\n', object_start) - object_start + 1);
    const auto malformed_path = directory / "malformed.manifest.txt";
    write_text(malformed_path, malformed);
    require_rejected_manifest(malformed_path, "malformed artifact manifest was accepted");

    const auto missing =
        s6::verify_cpu_build_evidence((directory / "missing.manifest").string());
    require(missing.identity_status() == s6::EvidenceStatus::blocked &&
                missing.graph_status() == s6::EvidenceStatus::blocked,
            "missing build evidence was not blocked");

    std::vector<float> dataset = {0.0F, 2.0F};
    std::vector<float> query = {0.0F};
    const s6::DatasetView dataset_view{dataset.data(), 2, 1, dataset.size()};
    const s6::QueryView query_view{query.data(), 1, query.size()};
    const auto missing_evidenced =
        s6::cpu_search_float32_evidenced(dataset_view, query_view, missing);
    const auto reference = s6::compute_float64_reference(dataset_view, query_view);
    s6::ValidationOptions missing_options;
    missing_options.production_evidence = &missing_evidenced.evidence;
    const auto missing_report = s6::validate_cpu_search(
        dataset_view, query_view, missing_evidenced.production, reference,
        missing_options);
    require(missing_report.evidence_identity_status == s6::EvidenceStatus::blocked &&
                missing_report.nn_verdict == s6::NnVerdict::blocked_unverified,
            "missing build identity did not block numerical acceptance");

    auto defective_production = missing_evidenced.production;
    defective_production.result->index = 1U;
    const auto defective_report = s6::validate_cpu_search(
        dataset_view, query_view, defective_production, reference,
        missing_options);
    require(defective_report.production_match_status == s6::EvidenceStatus::failed &&
                defective_report.nn_verdict == s6::NnVerdict::failed,
            "demonstrated numerical defect did not override missing identity evidence");

    const auto dirty_manifest = directory / "dirty-acceptance.manifest.txt";
    write_text(dirty_manifest,
               replace_field(original, "working_tree_state", "dirty"));
    const auto dirty_build =
        s6::verify_cpu_build_evidence(dirty_manifest.string());
    const auto evidenced =
        s6::cpu_search_float32_evidenced(dataset_view, query_view, dirty_build);
    s6::ValidationOptions options;
    options.production_evidence = &evidenced.evidence;
    const auto report = s6::validate_cpu_search(
        dataset_view, query_view, evidenced.production, reference, options);
    require(report.evidence_identity_status == s6::EvidenceStatus::failed &&
                report.nn_verdict == s6::NnVerdict::failed,
            "demonstrated dirty build identity did not fail numerical acceptance");
}

void replace_after(std::string& text, const std::string& anchor, const std::string& needle,
                   const std::string& replacement) {
    const auto anchor_position = text.find(anchor);
    require(anchor_position != std::string::npos, "graph mutation anchor missing");
    const auto position = text.find(needle, anchor_position);
    require(position != std::string::npos, "graph mutation instruction missing");
    text.replace(position, needle.size(), replacement);
}

std::string map_address(const std::string& map, const std::string& symbol) {
    const auto symbol_position = map.find(symbol);
    require(symbol_position != std::string::npos, "map symbol missing");
    const auto line_start = map.rfind('\n', symbol_position);
    const auto line_end = map.find('\n', symbol_position);
    const auto line = map.substr(line_start == std::string::npos ? 0 : line_start + 1,
                                 line_end - (line_start == std::string::npos ? 0 : line_start + 1));
    for (std::size_t index = 0; index + 16 <= line.size(); ++index) {
        const auto candidate = line.substr(index, 16);
        if (std::all_of(candidate.begin(), candidate.end(), [](unsigned char value) {
                return std::isxdigit(value) != 0;
            })) {
            auto normalized = candidate;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::toupper(value));
                           });
            return normalized;
        }
    }
    throw std::runtime_error("map address missing");
}

void test_graph_negative(const std::filesystem::path& manifest_path) {
    const auto manifest = read_text(manifest_path);
    const auto object_disassembly = read_text(field(manifest, "object_disassembly_path"));
    const auto executable_disassembly =
        read_text(field(manifest, "executable_disassembly_path"));
    const auto linker_map = read_text(field(manifest, "map_path"));
    const auto valid = s6::assess_graph_disassembly_for_testing(
        object_disassembly, executable_disassembly, linker_map);
    require(valid.status == s6::EvidenceStatus::passed &&
                valid.production_float32_status == s6::EvidenceStatus::passed &&
                valid.validator_float32_status == s6::EvidenceStatus::passed &&
                valid.reference_float64_status == s6::EvidenceStatus::passed &&
                valid.validator_float64_status == s6::EvidenceStatus::passed,
            "valid graph inspection fixture did not pass");

    auto tampered = object_disassembly;
    replace_after(tampered, "\n?production_distance@", "subss       xmm0",
                  "mulss       xmm0");
    const auto invalid = s6::assess_graph_disassembly_for_testing(
        tampered, executable_disassembly, linker_map);
    require(invalid.status == s6::EvidenceStatus::failed,
            "invalid graph evidence was not failed");

    auto invalid_validator32 = object_disassembly;
    replace_after(invalid_validator32, "\n?validator_distance@", "subss       xmm0",
                  "mulss       xmm0");
    require(s6::assess_graph_disassembly_for_testing(
                invalid_validator32, executable_disassembly, linker_map)
                .validator_float32_status == s6::EvidenceStatus::failed,
            "invalid validator Float32 graph was accepted");

    auto invalid_reference64 = object_disassembly;
    replace_after(invalid_reference64, "\n?reference_distance@", "subsd       xmm1",
                  "mulsd       xmm1");
    require(s6::assess_graph_disassembly_for_testing(
                invalid_reference64, executable_disassembly, linker_map)
                .reference_float64_status == s6::EvidenceStatus::failed,
            "invalid reference Float64 graph was accepted");

    auto wrong_reference_type = object_disassembly;
    replace_after(wrong_reference_type, "\n?reference_distance@", "subsd       xmm1",
                  "subss       xmm1");
    require(s6::assess_graph_disassembly_for_testing(
                wrong_reference_type, executable_disassembly, linker_map)
                .reference_float64_status == s6::EvidenceStatus::failed,
            "Float32 operation in reference Float64 graph was accepted");

    auto invalid_validator64 = object_disassembly;
    replace_after(invalid_validator64, "\n?validator_reference_distance@",
                  "subsd       xmm1", "mulsd       xmm1");
    require(s6::assess_graph_disassembly_for_testing(
                invalid_validator64, executable_disassembly, linker_map)
                .validator_float64_status == s6::EvidenceStatus::failed,
            "invalid validator Float64 graph was accepted");

    auto wrong_reference_fma = object_disassembly;
    replace_after(wrong_reference_fma, "\n?reference_distance@",
                  "call        qword ptr [__imp_fma]",
                  "call        qword ptr [__imp_fmaf]");
    require(s6::assess_graph_disassembly_for_testing(
                wrong_reference_fma, executable_disassembly, linker_map)
                .reference_float64_status == s6::EvidenceStatus::failed,
            "wrong Float64 FMA binding was accepted");

    auto missing_validator64 = object_disassembly;
    replace_after(missing_validator64, "\n?validator_reference_distance@",
                  "?validator_reference_distance@",
                  "?missing_validator_reference_distance@");
    require(s6::assess_graph_disassembly_for_testing(
                missing_validator64, executable_disassembly, linker_map)
                .validator_float64_status != s6::EvidenceStatus::passed,
            "missing validator Float64 graph was accepted");

    auto broken_accumulator = object_disassembly;
    replace_after(broken_accumulator, "\n?production_distance@",
                  "movaps      xmm2,xmm0",
                  "movaps      xmm3,xmm0");
    require(s6::assess_graph_disassembly_for_testing(
                broken_accumulator, executable_disassembly, linker_map).status ==
                s6::EvidenceStatus::failed,
            "broken accumulator dependency was accepted");

    auto broken_back_edge = object_disassembly;
    replace_after(broken_back_edge, "\n?production_distance@", "jb          ",
                  "je          ");
    require(s6::assess_graph_disassembly_for_testing(
                broken_back_edge, executable_disassembly, linker_map).status ==
                s6::EvidenceStatus::failed,
            "invalid production back-edge was accepted");

    auto decoy = object_disassembly;
    replace_after(decoy, "\n?production_distance@",
                  "call        qword ptr [__imp_fmaf]",
                  "call        qword ptr [__imp_sqrtf]");
    const auto production_heading = decoy.find("\n?production_distance@");
    require(production_heading != std::string::npos, "decoy production heading missing");
    const std::string decoy_text =
        "\n decoy xorps       xmm2,xmm2 movss       xmm0,dword ptr [rbp+rbx*4]"
        " subss       xmm0,dword ptr [rsi+rbx*4] movss       dword ptr [rsp+60h],xmm0"
        " movss       xmm1,dword ptr [rsp+60h] movss       xmm0,dword ptr [rsp+60h]"
        " call        qword ptr [__imp_fmaf] inc         rbx movaps      xmm2,xmm0"
        " cmp         rbx,rdi jb          0000000000000020\n";
    decoy.insert(decoy.find('\n', production_heading + 1) + 1, decoy_text);
    require(s6::assess_graph_disassembly_for_testing(
                decoy, executable_disassembly, linker_map).status ==
                s6::EvidenceStatus::failed,
            "decoy ordered graph text was accepted");

    const auto production_address = map_address(linker_map, "?production_distance@?A0");
    auto wrong_target = executable_disassembly;
    auto replacement_address = production_address;
    replacement_address.back() = replacement_address.back() == '0' ? '1' : '0';
    replace_after(wrong_target, map_address(linker_map, "?cpu_search_float32_impl@detail@s6@@"),
                  "call        " + production_address,
                  "call        " + replacement_address);
    require(s6::assess_graph_disassembly_for_testing(
                object_disassembly, wrong_target, linker_map).status ==
                s6::EvidenceStatus::failed,
            "wrong executable production call target was accepted");
}

void test_coverage_verifier() {
    const auto complete = s6::assess_candidate_visitation(4, {0, 1, 2, 3}, true);
    require(complete.status == s6::EvidenceStatus::passed,
            "complete coverage did not pass");
    require(s6::assess_candidate_visitation(4, {0, 1, 3}, true).status ==
                s6::EvidenceStatus::failed,
            "omitted candidate was not failed");
    require(s6::assess_candidate_visitation(4, {0, 1, 1, 2, 3}, true).status ==
                s6::EvidenceStatus::failed,
            "duplicate candidate was not failed");
    require(s6::assess_candidate_visitation(4, {0, 1, 2, 3, 4}, true).status ==
                s6::EvidenceStatus::failed,
            "out-of-range candidate was not failed");
    require(s6::assess_candidate_visitation(4, {0, 1}, false).status ==
                s6::EvidenceStatus::blocked,
            "incomplete invocation was not blocked");
}

void test_end_to_end_evidence(const s6::CpuBuildEvidence& build_evidence) {
    std::vector<float> dataset = {2.0F, 2.0F, 0.0F, 0.0F, -1.0F, 1.0F};
    std::vector<float> query = {0.0F, 0.0F};
    const s6::DatasetView dataset_view{dataset.data(), 3, 2, dataset.size()};
    const s6::QueryView query_view{query.data(), 2, query.size()};
    const auto evidenced =
        s6::cpu_search_float32_evidenced(dataset_view, query_view, build_evidence);
    const auto reference = s6::compute_float64_reference(dataset_view, query_view);
    s6::ValidationOptions options;
    options.production_evidence = &evidenced.evidence;
    s6::MandatoryGateEvidence gates;
    gates.fixture_status = s6::EvidenceStatus::passed;
    gates.oracle_status = s6::EvidenceStatus::passed;
    gates.tie_status = s6::EvidenceStatus::passed;
    gates.reduction_status = s6::EvidenceStatus::passed;
    gates.source_identity = build_evidence.source_identity();
    gates.fixture_ids = "evidence-unit-fixture,evidence-unit-oracle,evidence-unit-tie";
    options.mandatory_gate_evidence = &gates;
    const auto report = s6::validate_cpu_search(dataset_view, query_view,
                                                evidenced.production, reference, options);
    require(report.evidence_identity_status == s6::EvidenceStatus::passed &&
                report.graph_status == s6::EvidenceStatus::passed &&
                report.production_candidate_coverage_status ==
                    s6::EvidenceStatus::passed &&
                report.nn_verdict == s6::NnVerdict::accepted_unambiguous,
            "genuine verified production evidence was not accepted");
    require(report.numerical_paths.production_float32_graph ==
                    s6::EvidenceStatus::passed &&
                report.numerical_paths.validator_float32_graph ==
                    s6::EvidenceStatus::passed &&
                report.numerical_paths.reference_float64_graph ==
                    s6::EvidenceStatus::passed &&
                report.numerical_paths.validator_float64_graph ==
                    s6::EvidenceStatus::passed,
            "end-to-end validation did not bind all four numerical graphs");

    auto blocked_gates = gates;
    blocked_gates.tie_status = s6::EvidenceStatus::blocked;
    options.mandatory_gate_evidence = &blocked_gates;
    const auto missing_gate = s6::validate_cpu_search(
        dataset_view, query_view, evidenced.production, reference, options);
    require(missing_gate.nn_verdict == s6::NnVerdict::blocked_unverified,
            "missing mandatory tie evidence did not block acceptance");
    options.mandatory_gate_evidence = &gates;

    std::vector<float> other_dataset = dataset;
    other_dataset[0] = 3.0F;
    const s6::DatasetView other_view{other_dataset.data(), 3, 2, other_dataset.size()};
    const auto mismatched = s6::validate_cpu_search(other_view, query_view,
                                                    evidenced.production, reference, options);
    require(mismatched.evidence_identity_status == s6::EvidenceStatus::failed &&
                mismatched.nn_verdict == s6::NnVerdict::failed,
            "wrong workload identity was not rejected with production mismatch precedence");

    auto corrupt = evidenced.production;
    corrupt.result->index = 2;
    const auto failed = s6::validate_cpu_search(dataset_view, query_view, corrupt, reference,
                                                options);
    require(failed.nn_verdict == s6::NnVerdict::failed,
            "failed-over-blocked precedence was not preserved");

    s6::ValidationOptions missing;
    const auto blocked = s6::validate_cpu_search(dataset_view, query_view,
                                                 evidenced.production, reference, missing);
    require(blocked.nn_verdict == s6::NnVerdict::blocked_unverified,
            "missing evidence did not block");

    std::vector<float> tied_dataset = {-1.0F, 1.0F};
    std::vector<float> tied_query = {0.0F};
    const s6::DatasetView tied_view{tied_dataset.data(), 2, 1, tied_dataset.size()};
    const s6::QueryView tied_query_view{tied_query.data(), 1, tied_query.size()};
    const auto tied =
        s6::cpu_search_float32_evidenced(tied_view, tied_query_view, build_evidence);
    require(tied.production.result && tied.production.result->index == 0,
            "smallest-index production tie failed");

    const float maximum = std::numeric_limits<float>::max();
    const float negative_maximum = -maximum;
    const s6::DatasetView overflow_dataset{&maximum, 1, 1, 1};
    const s6::QueryView overflow_query{&negative_maximum, 1, 1};
    const auto aborted = s6::cpu_search_float32_evidenced(
        overflow_dataset, overflow_query, build_evidence);
    require(aborted.production.invocation_status == s6::InvocationStatus::backend_failure &&
                aborted.evidence.coverage_status() == s6::EvidenceStatus::blocked &&
                !aborted.evidence.coverage().completed,
            "aborted production evidence was not distinguished from exhaustive success");
}

void test_structured_evidence_negative(
    const s6::CpuBuildEvidence& build_evidence,
    const std::filesystem::path& manifest_path) {
    const auto directory = manifest_path.parent_path() / "negative-evidence" /
                           "structured-report";
    std::filesystem::create_directories(directory);
    std::vector<float> dataset = {0.0F, 0.0F, 2.0F, 2.0F};
    std::vector<float> query = {0.0F, 0.0F};
    const s6::DatasetView dataset_view{dataset.data(), 2, 2, dataset.size()};
    const s6::QueryView query_view{query.data(), 2, query.size()};
    const auto evidenced =
        s6::cpu_search_float32_evidenced(dataset_view, query_view, build_evidence);
    const auto reference = s6::compute_float64_reference(dataset_view, query_view);
    s6::MandatoryGateEvidence gates;
    gates.fixture_status = s6::EvidenceStatus::passed;
    gates.oracle_status = s6::EvidenceStatus::passed;
    gates.tie_status = s6::EvidenceStatus::passed;
    gates.reduction_status = s6::EvidenceStatus::passed;
    gates.source_identity = build_evidence.source_identity();
    gates.fixture_ids = "structured-fixture,structured-oracle,structured-tie";
    s6::ValidationOptions options;
    options.production_evidence = &evidenced.evidence;
    options.mandatory_gate_evidence = &gates;
    const auto report = s6::validate_cpu_search(
        dataset_view, query_view, evidenced.production, reference, options);
    require(report.nn_verdict == s6::NnVerdict::accepted_unambiguous,
            "structured negative-test baseline was not accepted");

    const std::vector<std::uint64_t> mathematical_distance_numerators = {
        0U, 32768U};
    s6::MatrixReportExpectation expectation;
    expectation.case_id = "negative-case";
    expectation.seed = 7U;
    expectation.dataset = dataset_view;
    expectation.query = query_view;
    expectation.build_evidence = &build_evidence;
    expectation.evidenced = &evidenced;
    expectation.reference = &reference;
    expectation.validation_report = &report;
    expectation.mandatory_gate_evidence = &gates;
    expectation.mathematical_distance_numerators =
        &mathematical_distance_numerators;
    expectation.mathematical_distance_exponent = -12;

    const auto write_report = [&]() {
        const auto summary = s6::write_cpu_matrix_case_report(
            directory.string(), "negative-case", 7U, dataset_view, query_view,
            build_evidence, evidenced, reference, report, gates,
            mathematical_distance_numerators, -12);
        require(summary.status == s6::EvidenceStatus::passed,
                "structured negative-test baseline report did not verify");
        return summary;
    };

    auto complete = write_report();
    require(s6::verify_cpu_matrix_case_report(complete.metadata_path).status ==
                s6::EvidenceStatus::blocked,
            "unbound report verification issued an accepted verdict");
    require(s6::verify_cpu_matrix_case_report(complete.metadata_path, expectation)
                    .status == s6::EvidenceStatus::passed,
            "complete workload-bound report did not pass strict verification");

    for (const auto& required_field :
         s6::cpu_matrix_required_metadata_fields()) {
        auto missing = write_report();
        write_text(missing.metadata_path,
                   remove_field(read_text(missing.metadata_path), required_field));
        require(s6::verify_cpu_matrix_case_report(missing.metadata_path, expectation)
                        .status == s6::EvidenceStatus::failed,
                ("missing required matrix field was accepted: " + required_field)
                    .c_str());

        auto corrupt = write_report();
        write_text(corrupt.metadata_path,
                   replace_field(read_text(corrupt.metadata_path), required_field,
                                 "corrupt"));
        require(s6::verify_cpu_matrix_case_report(corrupt.metadata_path, expectation)
                        .status == s6::EvidenceStatus::failed,
                ("corrupt required matrix field was accepted: " + required_field)
                    .c_str());
    }

    auto duplicate = write_report();
    auto duplicate_metadata = read_text(duplicate.metadata_path);
    duplicate_metadata += "revision=" + field(duplicate_metadata, "revision") + "\n";
    write_text(duplicate.metadata_path, duplicate_metadata);
    require(s6::verify_cpu_matrix_case_report(duplicate.metadata_path, expectation)
                    .status == s6::EvidenceStatus::failed,
            "duplicate required matrix field was accepted");

    auto wrong_expectation = expectation;
    wrong_expectation.seed = expectation.seed + 1U;
    complete = write_report();
    require(s6::verify_cpu_matrix_case_report(complete.metadata_path,
                                               wrong_expectation)
                    .status == s6::EvidenceStatus::failed,
            "report-to-workload expectation mismatch was accepted");

    auto summary = write_report();
    auto bytes = read_binary(summary.candidate_path);
    require(bytes.size() >= 124U, "candidate report too small for truncation test");
    bytes.resize(bytes.size() - 124U);
    write_binary(summary.candidate_path, bytes);
    auto metadata = read_text(summary.metadata_path);
    metadata = replace_field(metadata, "candidate_bytes", std::to_string(bytes.size()));
    metadata = replace_field(metadata, "candidate_sha256", sha256_file(summary.candidate_path));
    write_text(summary.metadata_path, metadata);
    require(s6::verify_cpu_matrix_case_report(summary.metadata_path).status ==
                s6::EvidenceStatus::failed,
            "missing candidate record was accepted");

    summary = write_report();
    bytes = read_binary(summary.candidate_path);
    constexpr std::size_t header_size = 24U;
    constexpr std::size_t record_size = 124U;
    require(bytes.size() >= header_size + 2U * record_size,
            "candidate report too small for duplicate test");
    const std::uint64_t duplicate_index = 0;
    std::memcpy(bytes.data() + header_size + record_size, &duplicate_index,
                sizeof(duplicate_index));
    write_binary(summary.candidate_path, bytes);
    metadata = replace_field(read_text(summary.metadata_path), "candidate_sha256",
                             sha256_file(summary.candidate_path));
    write_text(summary.metadata_path, metadata);
    require(s6::verify_cpu_matrix_case_report(summary.metadata_path).status ==
                s6::EvidenceStatus::failed,
            "duplicate candidate record was accepted");

    summary = write_report();
    bytes = read_binary(summary.candidate_path);
    const auto invalid_lower = double_bits(2.0);
    const auto invalid_upper = double_bits(1.0);
    constexpr std::size_t observed_lower_offset = header_size + 20U;
    constexpr std::size_t observed_upper_offset = header_size + 28U;
    std::memcpy(bytes.data() + observed_lower_offset, &invalid_lower,
                sizeof(invalid_lower));
    std::memcpy(bytes.data() + observed_upper_offset, &invalid_upper,
                sizeof(invalid_upper));
    write_binary(summary.candidate_path, bytes);
    metadata = replace_field(read_text(summary.metadata_path), "candidate_sha256",
                             sha256_file(summary.candidate_path));
    write_text(summary.metadata_path, metadata);
    require(s6::verify_cpu_matrix_case_report(summary.metadata_path).status ==
                s6::EvidenceStatus::failed,
            "invalid bound interval trace was accepted");

    summary = write_report();
    metadata = replace_field(read_text(summary.metadata_path), "tie_status", "failed");
    write_text(summary.metadata_path, metadata);
    require(s6::verify_cpu_matrix_case_report(summary.metadata_path).status ==
                s6::EvidenceStatus::failed,
            "accepted verdict with failed tie evidence was accepted");

    summary = write_report();
    metadata = read_text(summary.metadata_path);
    const std::string fixture_line = "fixture_ids=" + field(metadata, "fixture_ids") + "\n";
    const auto fixture_position = metadata.find(fixture_line);
    require(fixture_position != std::string::npos,
            "fixture provenance line missing in baseline report");
    metadata.erase(fixture_position, fixture_line.size());
    write_text(summary.metadata_path, metadata);
    require(s6::verify_cpu_matrix_case_report(summary.metadata_path).status ==
                s6::EvidenceStatus::failed,
            "missing fixture provenance was accepted");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "evidence manifest argument missing");
        const std::filesystem::path manifest_path = argv[1];
        const auto build_evidence = s6::verify_cpu_build_evidence(manifest_path.string());
        if (build_evidence.identity_status() != s6::EvidenceStatus::passed) {
            throw std::runtime_error("valid build identity did not pass: " +
                                     build_evidence.detail());
        }
        if (build_evidence.graph_status() != s6::EvidenceStatus::passed) {
            throw std::runtime_error("valid production graph did not pass: " +
                                     build_evidence.detail());
        }
        require(build_evidence.production_float32_graph_status() ==
                        s6::EvidenceStatus::passed &&
                    build_evidence.validator_float32_graph_status() ==
                        s6::EvidenceStatus::passed &&
                    build_evidence.reference_float64_graph_status() ==
                        s6::EvidenceStatus::passed &&
                    build_evidence.validator_float64_graph_status() ==
                        s6::EvidenceStatus::passed,
                "valid build did not verify all four numerical paths");
        test_manifest_rejection(manifest_path);
        test_graph_negative(manifest_path);
        test_coverage_verifier();
        test_end_to_end_evidence(build_evidence);
        test_structured_evidence_negative(build_evidence, manifest_path);
        std::cout << "S6 CPU evidence tests: PASS identity=" << build_evidence.identity()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "S6 CPU evidence tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
