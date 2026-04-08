// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.

#include <cstdint>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "cinnamon/cinnamon.h"

namespace py = pybind11;
using namespace Cinnamon;
using namespace Frontend;
using namespace std;

const char* const SAVE_DOC_STRING = R"DELIMITER(Serialize and save an Cinnamon object to a file.

Parameters
----------
path : str
    Path of the file to save to
)DELIMITER";

// clang-format off
PYBIND11_MODULE(_cinnamon, m) {
  m.doc() = "Python wrapper for Cinnamon";
  m.attr("__name__") = "cinnamon._cinnamon";

  py::enum_<Op>(m, "Op")
#define X(op,code) .value(#op, Op::op)
CINNAMON_OPS
#undef X
  ;
  py::enum_<Type>(m, "Type")
#define X(type,code) .value(#type, Type::type)
CINNAMON_TYPES
#undef X
  ;
  py::class_<Term, shared_ptr<Term>>(m, "Term", "Cinnamon's native Term class")
    .def_property_readonly("op", &Term::getOp, "The operation performed by this term")
    .def_property_readonly("scale", &Term::getScale, "The Scale of this term")
    .def_property_readonly("level", &Term::getLevel, "The Level of this term");


  py::class_<Program>(m, "Program", "Cinnamon's Program class")
    .def(py::init<string,uint32_t,uint8_t>(), py::arg("name"), py::arg("rns_bit_size")=28, py::arg("num_chips")=1)
    .def_property("name", &Program::getName, &Program::setName, "The name of this program")
    .def("_currentPartitionSize", &Program::getCurrentPartitionSize, "" )
    .def("_currentPartitionID", &Program::getCurrentPartitionId, "")
    .def("to_DOT", &Program::toDOT, R"DELIMITER(Produce a graph representation of the program in the DOT format.

Returns
-------
str
    The graph in DOT format)DELIMITER")
    .def("_make_add", &Program::makeAdd, py::keep_alive<0,1>())
    .def("_make_subtract", &Program::makeSubtract, py::keep_alive<0,1>())
    .def("_make_multiply", &Program::makeMultiply, py::keep_alive<0,1>())
    .def("_make_negate", &Program::makeNegate, py::keep_alive<0,1>())
    .def("_make_left_rotation", &Program::makeLeftRotation, py::keep_alive<0,1>())
    .def("_make_right_rotation", &Program::makeRightRotation, py::keep_alive<0,1>())
    .def("_make_conjugate", &Program::makeConjugate, py::keep_alive<0,1>())
    .def("_make_rotate_multiply_accumulate", &Program::makeRotateMultiplyAccumulate, py::keep_alive<0,1>())
    .def("_make_multiply_rotate_accumulate", &Program::makeMultiplyRotateAccumulate, py::keep_alive<0,1>())
    .def("_make_rotate_accumulate", &Program::makeRotateAccumulate, py::keep_alive<0,1>())
    .def("_make_bsgs_multiply_accumulate", &Program::makeBsgsMultiplyAccumulate, py::keep_alive<0,1>())
    .def("_make_plaintext_input", &Program::makePlaintextInput, py::keep_alive<0,1>())
    .def("_make_ciphertext_input", &Program::makeCiphertextInput, py::keep_alive<0,1>())
    .def("_make_receive", &Program::makeReceive, py::keep_alive<0,1>())
    .def("_make_rescale", &Program::makeRescale, py::keep_alive<0,1>())
    .def("_make_ephemeral", &Program::makeToEphemeral, py::keep_alive<0,1>())
    .def("_make_relinearize", &Program::makeRelinearize, py::keep_alive<0,1>())
    .def("_make_relinearize2", &Program::makeRelinearize2, py::keep_alive<0,1>())
    .def("_make_modswitch", &Program::makeModSwitch, py::keep_alive<0,1>())
    .def("_make_bootstrap_modraise", &Program::makeBootstrapModRaise, py::keep_alive<0,1>())
    .def("_make_output", &Program::makeOutput, py::keep_alive<0,1>())
    .def("_make_partition", &Program::makePartition, py::keep_alive<0,1>());

  py::module m_passes = m.def_submodule("_passes", "Python wrapper for Cinnamon Passes");
  m_passes.def("keyswitch_pass", &Backend::keyswitchPass, "", py::arg("program"))
              ;

  py::module m_compiler = m.def_submodule("_compiler", "Python wrapper for Cinnamon Compiler");
  m_compiler.def("cinnamon_compile", &Backend::cinnamonCompile, "", py::arg("program"), py::arg("levels"), py::arg("num_chips"), py::arg("num_vregs"), py::arg("output_prefix"), py::arg("use_cinnamon_keyswitching")= true)
              ;

}
// clang-format on
