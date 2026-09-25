#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <utf8proc.h>

namespace {

constexpr std::size_t kDefaultMaxLength = 256;
constexpr std::size_t kMaxWordChars = 100;
constexpr std::size_t kEmbeddingDimensions = 384;

struct Vocabulary {
    std::unordered_map<std::string, std::int64_t> ids;
    std::int64_t unk = -1;
    std::int64_t cls = -1;
    std::int64_t sep = -1;
    std::int64_t pad = -1;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        fail("unable to open input file: " + path);
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

Vocabulary load_vocab(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        fail("unable to open vocab file: " + path);
    }
    Vocabulary vocab;
    std::string token;
    std::int64_t index = 0;
    while (std::getline(input, token)) {
        if (!token.empty() && token.back() == '\r') {
            token.pop_back();
        }
        vocab.ids.emplace(token, index++);
    }
    auto require = [&](const char* token_name) -> std::int64_t {
        const auto it = vocab.ids.find(token_name);
        if (it == vocab.ids.end()) {
            fail(std::string("vocab is missing required token: ") + token_name);
        }
        return it->second;
    };
    vocab.unk = require("[UNK]");
    vocab.cls = require("[CLS]");
    vocab.sep = require("[SEP]");
    vocab.pad = require("[PAD]");
    return vocab;
}

std::vector<utf8proc_int32_t> decode_utf8(std::string_view text) {
    std::vector<utf8proc_int32_t> out;
    const auto* ptr = reinterpret_cast<const utf8proc_uint8_t*>(text.data());
    std::size_t offset = 0;
    while (offset < text.size()) {
        utf8proc_int32_t cp = 0;
        const auto consumed = utf8proc_iterate(ptr + offset, static_cast<utf8proc_ssize_t>(text.size() - offset), &cp);
        if (consumed <= 0) {
            fail("invalid UTF-8 input");
        }
        out.push_back(cp);
        offset += static_cast<std::size_t>(consumed);
    }
    return out;
}

void append_codepoint(std::string& out, utf8proc_int32_t cp) {
    std::array<utf8proc_uint8_t, 4> buffer{};
    const auto written = utf8proc_encode_char(cp, buffer.data());
    if (written <= 0) {
        fail("unable to encode Unicode codepoint");
    }
    out.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(written));
}

bool is_whitespace(utf8proc_int32_t cp) {
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
        return true;
    }
    return utf8proc_category(cp) == UTF8PROC_CATEGORY_ZS;
}

bool is_control(utf8proc_int32_t cp) {
    if (cp == '\t' || cp == '\n' || cp == '\r') {
        return false;
    }
    switch (utf8proc_category(cp)) {
        case UTF8PROC_CATEGORY_CC:
        case UTF8PROC_CATEGORY_CF:
        case UTF8PROC_CATEGORY_CS:
        case UTF8PROC_CATEGORY_CO:
        case UTF8PROC_CATEGORY_CN:
            return true;
        default:
            return false;
    }
}

bool is_punctuation(utf8proc_int32_t cp) {
    if ((cp >= 33 && cp <= 47) || (cp >= 58 && cp <= 64) ||
        (cp >= 91 && cp <= 96) || (cp >= 123 && cp <= 126)) {
        return true;
    }
    switch (utf8proc_category(cp)) {
        case UTF8PROC_CATEGORY_PC:
        case UTF8PROC_CATEGORY_PD:
        case UTF8PROC_CATEGORY_PE:
        case UTF8PROC_CATEGORY_PF:
        case UTF8PROC_CATEGORY_PI:
        case UTF8PROC_CATEGORY_PO:
        case UTF8PROC_CATEGORY_PS:
            return true;
        default:
            return false;
    }
}

bool is_chinese_char(utf8proc_int32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x20000 && cp <= 0x2A6DF) ||
           (cp >= 0x2A700 && cp <= 0x2B73F) ||
           (cp >= 0x2B740 && cp <= 0x2B81F) ||
           (cp >= 0x2B820 && cp <= 0x2CEAF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0x2F800 && cp <= 0x2FA1F);
}

std::vector<std::string> whitespace_tokenize(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    for (const auto cp : decode_utf8(text)) {
        if (is_whitespace(cp)) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        } else {
            append_codepoint(current, cp);
        }
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

std::string clean_and_tokenize_chinese(std::string_view text) {
    std::string out;
    for (const auto cp : decode_utf8(text)) {
        if (cp == 0 || cp == 0xFFFD || is_control(cp)) {
            continue;
        }
        if (is_whitespace(cp)) {
            out.push_back(' ');
            continue;
        }
        if (is_chinese_char(cp)) {
            out.push_back(' ');
            append_codepoint(out, cp);
            out.push_back(' ');
            continue;
        }
        append_codepoint(out, cp);
    }
    return out;
}

std::string lowercase_strip_accents(std::string_view token) {
    std::string out;
    constexpr auto options = static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_DECOMPOSE);
    for (const auto original : decode_utf8(token)) {
        const auto lower = utf8proc_tolower(original);
        std::array<utf8proc_int32_t, 8> decomposed{};
        auto count = utf8proc_decompose_char(lower, decomposed.data(), static_cast<utf8proc_ssize_t>(decomposed.size()), options, nullptr);
        if (count < 0) {
            fail(std::string("Unicode decomposition failed: ") + utf8proc_errmsg(count));
        }
        if (count > static_cast<utf8proc_ssize_t>(decomposed.size())) {
            std::vector<utf8proc_int32_t> dynamic(static_cast<std::size_t>(count));
            count = utf8proc_decompose_char(lower, dynamic.data(), count, options, nullptr);
            if (count < 0) {
                fail(std::string("Unicode decomposition failed: ") + utf8proc_errmsg(count));
            }
            for (utf8proc_ssize_t i = 0; i < count; ++i) {
                if (utf8proc_category(dynamic[static_cast<std::size_t>(i)]) != UTF8PROC_CATEGORY_MN) {
                    append_codepoint(out, dynamic[static_cast<std::size_t>(i)]);
                }
            }
        } else {
            for (utf8proc_ssize_t i = 0; i < count; ++i) {
                if (utf8proc_category(decomposed[static_cast<std::size_t>(i)]) != UTF8PROC_CATEGORY_MN) {
                    append_codepoint(out, decomposed[static_cast<std::size_t>(i)]);
                }
            }
        }
    }
    return out;
}

std::vector<std::string> split_on_punctuation(std::string_view token) {
    std::vector<std::string> pieces;
    std::string current;
    for (const auto cp : decode_utf8(token)) {
        if (is_punctuation(cp)) {
            if (!current.empty()) {
                pieces.push_back(std::move(current));
                current.clear();
            }
            std::string punct;
            append_codepoint(punct, cp);
            pieces.push_back(std::move(punct));
        } else {
            append_codepoint(current, cp);
        }
    }
    if (!current.empty()) {
        pieces.push_back(std::move(current));
    }
    return pieces;
}

bool is_special_token(std::string_view token) {
    return token == "[UNK]" || token == "[SEP]" || token == "[PAD]" || token == "[CLS]" || token == "[MASK]";
}

std::vector<std::string> basic_tokenize(std::string_view text) {
    std::vector<std::string> result;
    const auto cleaned = clean_and_tokenize_chinese(text);
    for (const auto& raw : whitespace_tokenize(cleaned)) {
        if (is_special_token(raw)) {
            result.push_back(raw);
            continue;
        }
        const auto normalized = lowercase_strip_accents(raw);
        auto pieces = split_on_punctuation(normalized);
        result.insert(result.end(), std::make_move_iterator(pieces.begin()), std::make_move_iterator(pieces.end()));
    }
    return result;
}

std::vector<std::int64_t> wordpiece(const Vocabulary& vocab, std::string_view token) {
    const auto cps = decode_utf8(token);
    if (cps.size() > kMaxWordChars) {
        return {vocab.unk};
    }

    std::vector<std::size_t> byte_offsets;
    byte_offsets.reserve(cps.size() + 1);
    byte_offsets.push_back(0);
    const auto* ptr = reinterpret_cast<const utf8proc_uint8_t*>(token.data());
    std::size_t offset = 0;
    while (offset < token.size()) {
        utf8proc_int32_t cp = 0;
        const auto consumed = utf8proc_iterate(ptr + offset, static_cast<utf8proc_ssize_t>(token.size() - offset), &cp);
        if (consumed <= 0) {
            return {vocab.unk};
        }
        offset += static_cast<std::size_t>(consumed);
        byte_offsets.push_back(offset);
    }

    std::vector<std::int64_t> ids;
    std::size_t start = 0;
    while (start < cps.size()) {
        bool matched = false;
        for (std::size_t end = cps.size(); end > start; --end) {
            std::string candidate;
            if (start != 0) {
                candidate = "##";
            }
            candidate.append(token.substr(byte_offsets[start], byte_offsets[end] - byte_offsets[start]));
            const auto it = vocab.ids.find(candidate);
            if (it != vocab.ids.end()) {
                ids.push_back(it->second);
                start = end;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return {vocab.unk};
        }
    }
    return ids;
}

std::vector<std::int64_t> tokenize_ids(const Vocabulary& vocab, std::string_view text, std::size_t max_length) {
    if (max_length < 2) {
        fail("max length must be at least 2");
    }
    std::vector<std::int64_t> ids;
    ids.reserve(std::min<std::size_t>(max_length, 64));
    ids.push_back(vocab.cls);
    for (const auto& token : basic_tokenize(text)) {
        auto pieces = wordpiece(vocab, token);
        for (const auto id : pieces) {
            if (ids.size() + 1 >= max_length) {
                ids.push_back(vocab.sep);
                return ids;
            }
            ids.push_back(id);
        }
    }
    ids.push_back(vocab.sep);
    return ids;
}

struct NativeEncoder {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "acclorite-m11-native-onnx"};
    Ort::SessionOptions options;
    Ort::Session session{nullptr};
    std::vector<std::string> input_names_owned;
    std::string output_name_owned;

    explicit NativeEncoder(const std::string& model_path, int threads) {
        if (threads > 0) {
            options.SetIntraOpNumThreads(threads);
            options.SetInterOpNumThreads(1);
        }
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session = Ort::Session(env, model_path.c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;
        const auto input_count = session.GetInputCount();
        input_names_owned.reserve(input_count);
        for (std::size_t i = 0; i < input_count; ++i) {
            auto name = session.GetInputNameAllocated(i, allocator);
            input_names_owned.emplace_back(name.get());
        }

        const auto output_count = session.GetOutputCount();
        if (output_count == 0) {
            fail("ONNX model has no outputs");
        }
        for (std::size_t i = 0; i < output_count; ++i) {
            auto name = session.GetOutputNameAllocated(i, allocator);
            if (i == 0 || std::string_view(name.get()) == "last_hidden_state") {
                output_name_owned = name.get();
            }
            if (std::string_view(name.get()) == "last_hidden_state") {
                break;
            }
        }
    }

    std::vector<std::vector<float>> encode(const std::vector<std::vector<std::int64_t>>& token_rows, std::int64_t pad_id) {
        if (token_rows.empty()) {
            return {};
        }
        const std::size_t batch = token_rows.size();
        std::size_t seq = 0;
        for (const auto& row : token_rows) {
            seq = std::max(seq, row.size());
        }
        if (seq == 0) {
            fail("tokenizer produced an empty sequence");
        }

        std::vector<std::int64_t> input_ids(batch * seq, pad_id);
        std::vector<std::int64_t> attention_mask(batch * seq, 0);
        std::vector<std::int64_t> token_type_ids(batch * seq, 0);
        for (std::size_t b = 0; b < batch; ++b) {
            for (std::size_t s = 0; s < token_rows[b].size(); ++s) {
                input_ids[b * seq + s] = token_rows[b][s];
                attention_mask[b * seq + s] = 1;
            }
        }

        const std::array<std::int64_t, 2> shape{static_cast<std::int64_t>(batch), static_cast<std::int64_t>(seq)};
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor_for = [&](std::vector<std::int64_t>& data) {
            return Ort::Value::CreateTensor<std::int64_t>(
                memory,
                data.data(),
                data.size(),
                shape.data(),
                shape.size()
            );
        };

        std::vector<Ort::Value> input_values;
        std::vector<const char*> input_names;
        input_values.reserve(input_names_owned.size());
        input_names.reserve(input_names_owned.size());
        for (const auto& name : input_names_owned) {
            input_names.push_back(name.c_str());
            if (name == "input_ids") {
                input_values.emplace_back(tensor_for(input_ids));
            } else if (name == "attention_mask") {
                input_values.emplace_back(tensor_for(attention_mask));
            } else if (name == "token_type_ids") {
                input_values.emplace_back(tensor_for(token_type_ids));
            } else {
                fail("unsupported ONNX input: " + name);
            }
        }

        const char* output_name = output_name_owned.c_str();
        auto outputs = session.Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            input_values.data(),
            input_values.size(),
            &output_name,
            1
        );
        if (outputs.size() != 1 || !outputs[0].IsTensor()) {
            fail("ONNX model did not return one tensor output");
        }
        const auto out_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (out_shape.size() != 3 || out_shape[0] != static_cast<std::int64_t>(batch) ||
            out_shape[1] != static_cast<std::int64_t>(seq) || out_shape[2] <= 0) {
            fail("unexpected ONNX output shape");
        }
        const std::size_t dims = static_cast<std::size_t>(out_shape[2]);
        if (dims != kEmbeddingDimensions) {
            fail("unexpected embedding dimension: " + std::to_string(dims));
        }
        const float* hidden = outputs[0].GetTensorData<float>();

        std::vector<std::vector<float>> embeddings(batch, std::vector<float>(dims, 0.0F));
        for (std::size_t b = 0; b < batch; ++b) {
            float token_count = 0.0F;
            for (std::size_t s = 0; s < seq; ++s) {
                if (attention_mask[b * seq + s] == 0) {
                    continue;
                }
                token_count += 1.0F;
                const auto base = (b * seq + s) * dims;
                for (std::size_t d = 0; d < dims; ++d) {
                    embeddings[b][d] += hidden[base + d];
                }
            }
            if (token_count <= 0.0F) {
                fail("cannot mean-pool an empty attention mask");
            }
            double norm_sq = 0.0;
            for (auto& value : embeddings[b]) {
                value /= token_count;
                norm_sq += static_cast<double>(value) * static_cast<double>(value);
            }
            const auto norm = std::sqrt(norm_sq);
            if (!(norm > 0.0) || !std::isfinite(norm)) {
                fail("invalid embedding norm");
            }
            for (auto& value : embeddings[b]) {
                value = static_cast<float>(static_cast<double>(value) / norm);
            }
        }
        return embeddings;
    }
};

void print_usage() {
    std::cout
        << "usage: acclorite_m11_native_onnx_encoder --vocab PATH --input PATH [--model PATH] [--max-length N] [--threads N] [--tokenize-only]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string model_path;
        std::string vocab_path;
        std::string input_path;
        std::size_t max_length = kDefaultMaxLength;
        int threads = 0;
        bool tokenize_only = false;

        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            auto require_value = [&](const char* flag) -> std::string {
                if (i + 1 >= argc) {
                    fail(std::string(flag) + " requires a value");
                }
                return argv[++i];
            };
            if (arg == "--model") {
                model_path = require_value("--model");
            } else if (arg == "--vocab") {
                vocab_path = require_value("--vocab");
            } else if (arg == "--input") {
                input_path = require_value("--input");
            } else if (arg == "--max-length") {
                max_length = static_cast<std::size_t>(std::stoul(require_value("--max-length")));
            } else if (arg == "--threads") {
                threads = std::stoi(require_value("--threads"));
                if (threads < 0) {
                    fail("--threads must be >= 0");
                }
            } else if (arg == "--tokenize-only") {
                tokenize_only = true;
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            } else {
                fail("unknown argument: " + std::string(arg));
            }
        }

        if (vocab_path.empty() || input_path.empty() || (!tokenize_only && model_path.empty())) {
            print_usage();
            return 2;
        }
        if (max_length < 2 || max_length > 512) {
            fail("--max-length must be between 2 and 512");
        }

        const auto vocab = load_vocab(vocab_path);
        const auto lines = read_lines(input_path);
        std::vector<std::vector<std::int64_t>> token_rows;
        token_rows.reserve(lines.size());
        for (const auto& line : lines) {
            token_rows.push_back(tokenize_ids(vocab, line, max_length));
        }

        if (tokenize_only) {
            for (std::size_t i = 0; i < token_rows.size(); ++i) {
                std::cout << "@tokens\t" << i << '\t';
                for (std::size_t j = 0; j < token_rows[i].size(); ++j) {
                    if (j != 0) {
                        std::cout << ',';
                    }
                    std::cout << token_rows[i][j];
                }
                std::cout << '\n';
            }
            return 0;
        }

        NativeEncoder encoder(model_path, threads);
        const auto embeddings = encoder.encode(token_rows, vocab.pad);
        std::cout << "@meta\trows\t" << embeddings.size() << "\tdims\t" << kEmbeddingDimensions << '\n';
        std::cout << std::setprecision(9);
        for (std::size_t i = 0; i < embeddings.size(); ++i) {
            std::cout << "@embedding\t" << i;
            for (const auto value : embeddings[i]) {
                std::cout << '\t' << value;
            }
            std::cout << '\n';
        }
        return 0;
    } catch (const Ort::Exception& exc) {
        std::cerr << "error: ONNX Runtime: " << exc.what() << '\n';
        return 3;
    } catch (const std::exception& exc) {
        std::cerr << "error: " << exc.what() << '\n';
        return 2;
    }
}
