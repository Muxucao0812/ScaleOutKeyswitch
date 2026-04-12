// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <complex>
#include <cstdint>
#include <iostream>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include "cinnamon_emulator/ckks-encoder.h"
#include "cinnamon_emulator/context.h"
#include "cinnamon_emulator/emulator.h"
#include "cinnamon_emulator/rns_polynomial.h"

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

using namespace Cinnamon::Emulator;
namespace py = pybind11;

using SerializedLimb =
    std::tuple<std::uint64_t, bool, std::vector<std::uint32_t>>;

PYBIND11_MODULE(_cinnamon_emulator, m) {
    m.attr("__name__") = "cinnamon_emulator._cinnamon_emulator";

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif

    py::class_<Cinnamon::Emulator::Context>(m, "Context", "Cinnamon Emulator Context")
        .def(py::init([](const size_t slots, const std::vector<std::uint64_t> &rns_modulii) {
            return std::unique_ptr<Cinnamon::Emulator::Context>(new Cinnamon::Emulator::Context(slots, rns_modulii));
        }))
        .def_property_readonly("slots", &Cinnamon::Emulator::Context::slots)
        .def_property_readonly("coeff_count", &Cinnamon::Emulator::Context::n)
        .def_property_readonly("num_rns_bases", &Cinnamon::Emulator::Context::num_rns_bases)
        .def_property_readonly("rns_moduli", [](const Cinnamon::Emulator::Context &context) {
            std::vector<std::uint64_t> moduli;
            moduli.reserve(context.num_rns_bases());
            for (const auto &modulus : context.rns_bases()) {
                moduli.push_back(static_cast<std::uint64_t>(modulus.value()));
            }
            return moduli;
        });

    py::class_<Cinnamon::Emulator::CKKSEncoder>(m, "CKKSEncoder", "Cinnamon Emulator CKKS Encoder")
        .def(py::init([](const Cinnamon::Emulator::Context &context) {
            return std::unique_ptr<Cinnamon::Emulator::CKKSEncoder>(new Cinnamon::Emulator::CKKSEncoder(context));
        }))
        .def(py::init([](const Cinnamon::Emulator::Context &context, const seal::prng_seed_type &prng_seed) {
            return std::unique_ptr<Cinnamon::Emulator::CKKSEncoder>(new Cinnamon::Emulator::CKKSEncoder(context, prng_seed));
        }))
        .def("encode", [](Cinnamon::Emulator::CKKSEncoder &encoder, const Cinnamon::Emulator::Emulator::MessageType &message, const double scale, const std::vector<uint64_t> rns_base_ids) {
            return std::visit(overloaded{
                                  [](auto arg) { throw std::invalid_argument("Message"); },
                                  [&](const double &arg) { return encoder.encode(arg,  scale,  rns_base_ids); },
                                  [&](const std::vector<double> &arg) { return encoder.encode(arg,  scale,  rns_base_ids); },
                                  [&](const std::vector<std::complex<double>> &arg) { return encoder.encode(arg,  scale,  rns_base_ids); }},
                              message);
        })
        .def("encode_as_polynomial", [](Cinnamon::Emulator::CKKSEncoder &encoder, const Cinnamon::Emulator::Emulator::MessageType &message, const double scale) {
            return std::visit(overloaded{
                                  [](auto arg) { throw std::invalid_argument("Message"); },
                                  [&](const double &arg) { throw std::invalid_argument("Unimplemented: Encode constant as polynomial"); return std::vector<double>(); },
                                  [&](const std::vector<double> &arg) { return encoder.encode_as_polynomial(arg,  scale); },
                                  [&](const std::vector<std::complex<double>> &arg) { return encoder.encode_as_polynomial(arg,  scale); }},
                              message);
        })
        .def("decode", [](Cinnamon::Emulator::CKKSEncoder &encoder, const std::shared_ptr<Cinnamon::Emulator::RnsPolynomial> &input, const double scale) {
            return encoder.decode<double>(input, scale);
        })
        .def("decode_complex", [](Cinnamon::Emulator::CKKSEncoder &encoder, const std::shared_ptr<Cinnamon::Emulator::RnsPolynomial> &input, const double scale) {
            return encoder.decode<std::complex<double>>(input, scale);
        });

    py::class_<Cinnamon::Emulator::CKKSEncryptor>(m, "CKKSEncryptor", "Cinnamon Emulator CKKS Encryptor")
        .def(py::init([](const Cinnamon::Emulator::Context &context, const std::vector<std::int64_t> &secret_key) {
            return std::unique_ptr<Cinnamon::Emulator::CKKSEncryptor>(new Cinnamon::Emulator::CKKSEncryptor(context, secret_key, secret_key));
        }))
        .def(py::init([](const Cinnamon::Emulator::Context &context, const std::vector<std::int64_t> &secret_key, const seal::prng_seed_type &prng_seed) {
            return std::unique_ptr<Cinnamon::Emulator::CKKSEncryptor>(new Cinnamon::Emulator::CKKSEncryptor(context, secret_key, secret_key, prng_seed));
        }))
        .def(py::init([](const Cinnamon::Emulator::Context &context, const std::vector<std::int64_t> &secret_key, const std::vector<std::int64_t> &ephemeral_key, const seal::prng_seed_type &prng_seed) {
            return std::unique_ptr<Cinnamon::Emulator::CKKSEncryptor>(new Cinnamon::Emulator::CKKSEncryptor(context, secret_key, ephemeral_key, prng_seed));
        }))
        .def("encode_and_encrypt", [](Cinnamon::Emulator::CKKSEncryptor &encryptor, const Cinnamon::Emulator::Emulator::MessageType &message, const double scale, const std::vector<uint64_t> rns_base_ids) {
            return std::visit(overloaded{
                                  [](auto arg) { throw std::invalid_argument("Message"); },
                                  [&](const double &arg) { return encryptor.encode_and_encrypt(arg,  scale,  rns_base_ids); },
                                  [&](const std::vector<double> &arg) { return encryptor.encode_and_encrypt(arg,  scale,  rns_base_ids); },
                                  [&](const std::vector<std::complex<double>> &arg) { return encryptor.encode_and_encrypt(arg,  scale,  rns_base_ids); }},
                              message);
        })
        .def("decrypt_and_decode", [](Cinnamon::Emulator::CKKSEncryptor &encryptor, const std::pair<RnsPolynomialPtr, RnsPolynomialPtr> &input, const std::vector<uint64_t> &rns_base_ids, const double scale) {
            return encryptor.decrypt_and_decode<double>(input, rns_base_ids, scale);
        });

    py::class_<Cinnamon::Emulator::Emulator>(m, "Emulator", "Cinnamon CKKS Emulator")
        .def(py::init([](const Cinnamon::Emulator::Context &context) {
            return std::unique_ptr<Cinnamon::Emulator::Emulator>(new Cinnamon::Emulator::Emulator(context));
        }))
        .def("set_program_memory", [](Cinnamon::Emulator::Emulator &emulator,
                                      const std::unordered_map<std::string, SerializedLimb> &memory) {
            std::unordered_map<std::string, std::shared_ptr<Cinnamon::Emulator::Limb>> native_memory;
            native_memory.reserve(memory.size());
            for (const auto &entry : memory) {
                const auto &term = entry.first;
                const auto &serialized = entry.second;
                const auto &rns_base_id = std::get<0>(serialized);
                const auto &is_ntt_form = std::get<1>(serialized);
                const auto &coeffs = std::get<2>(serialized);
                native_memory[term] = std::make_shared<Cinnamon::Emulator::Limb>(
                    coeffs, rns_base_id, is_ntt_form);
            }
            emulator.set_program_memory(native_memory);
        })
        .def("get_program_memory", [](Cinnamon::Emulator::Emulator &emulator) {
            std::unordered_map<std::string, SerializedLimb> serialized_memory;
            const auto native_memory = emulator.get_program_memory();
            serialized_memory.reserve(native_memory.size());
            for (const auto &entry : native_memory) {
                const auto &term = entry.first;
                const auto &limb = entry.second;
                std::vector<std::uint32_t> coeffs;
                coeffs.reserve(limb->size());
                const auto *data = limb->data();
                for (std::size_t i = 0; i < limb->size(); ++i) {
                    coeffs.push_back(data[i]);
                }
                serialized_memory[term] = std::make_tuple(
                    limb->rns_base_id(),
                    limb->is_ntt_form(),
                    std::move(coeffs));
            }
            return serialized_memory;
        })
        .def("generate_inputs", &Cinnamon::Emulator::Emulator::generate_inputs)
        .def("generate_and_serialize_evalkeys", &Cinnamon::Emulator::Emulator::generate_and_serialize_evalkeys)
        .def("run_program", &Cinnamon::Emulator::Emulator::run_program_multithread)
        .def("get_decrypted_outputs", &Cinnamon::Emulator::Emulator::get_decrypted_outputs<std::complex<double>>)
        .def("decrypt_and_print_outputs", &Cinnamon::Emulator::Emulator::decrypt_and_print_outputs);
}
