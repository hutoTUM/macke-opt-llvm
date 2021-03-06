import unittest
import os
import subprocess


class TestPointerlogic(unittest.TestCase):

    def run_pass_test(self, bitcodefile, new_entrypoint, assertions):
        self.assertIn("LLVMBIN", os.environ, "Path to llvm-bin not set")
        self.assertIn("KLEEBIN", os.environ, "Path to klee-bin not set")

        modfilename = "bin/mod-" + new_entrypoint + ".bc"

        subprocess.check_output([
            os.environ["LLVMBIN"] + "/opt",
            "-load", "bin/libMackeOpt.so",
            "-encapsulatesymbolic", bitcodefile,
            "-encapsulatedfunction", new_entrypoint,
            "-o", modfilename])

        # --disable-internalize can be removed, if KLEE fixes bug #454
        out = subprocess.check_output([
            os.environ["KLEEBIN"] + "/klee",
            "--only-output-states-covering-new", "--optimize",
            "--entry-point", "macke_%s_main" % new_entrypoint,
            "--disable-internalize", "--search=nurs:covnew", modfilename],
            stderr=subprocess.STDOUT)

        for assertion in assertions:
            self.assertIn(assertion, out.decode("utf-8"))

    def test_not42(self):
        self.run_pass_test(
            "bin/klee_objsize.bc", "assert4allSizes",
            ["ASSERTION FAIL: (1 * sizeof(int)) != klee_get_obj_size(ptr)",
             "ASSERTION FAIL: (2 * sizeof(int)) != klee_get_obj_size(ptr)",
             "ASSERTION FAIL: (4 * sizeof(int)) != klee_get_obj_size(ptr)",
             "ASSERTION FAIL: (16 * sizeof(int)) != klee_get_obj_size(ptr)",
             "ASSERTION FAIL: (128 * sizeof(int)) != klee_get_obj_size(ptr)"]
        )
