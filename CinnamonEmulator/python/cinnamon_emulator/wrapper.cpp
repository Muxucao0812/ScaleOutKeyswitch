// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <complex>
#include <iostream>
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
        .def_property_readonly("coeff_count", &Cinnamon::Emulator::Context::n);

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
        .def("set_program_memory", &Cinnamon::Emulator::Emulator::set_program_memory)
        .def("get_program_memory", &Cinnamon::Emulator::Emulator::get_program_memory)
        .def("generate_inputs", &Cinnamon::Emulator::Emulator::generate_inputs)
        .def("generate_and_serialize_evalkeys", &Cinnamon::Emulator::Emulator::generate_and_serialize_evalkeys)
        .def("run_program", &Cinnamon::Emulator::Emulator::run_program_multithread)
        .def("get_decrypted_outputs", &Cinnamon::Emulator::Emulator::get_decrypted_outputs<std::complex<double>>)
        .def("decrypt_and_print_outputs", &Cinnamon::Emulator::Emulator::decrypt_and_print_outputs);
}
