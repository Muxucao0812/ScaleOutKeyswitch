from cinnamon.dsl import *

RNS_BIT_SIZE=28

def testAdd(numChips):
    program = CinnamonProgram('Add',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        c1 = CiphertextInput('x',scale,0)
        c2 = CiphertextInput('y',scale,0)
        c1 = c1 + c2
        c1 = c1 + c2
        Output('z', c1)
    return program

def testAddScalar(numChips):
    program = CinnamonProgram('AddScalar',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        c1 = CiphertextInput('x', scale, 0)
        c2 = PlaintextInput('s', scale, 0, scalar=True)
        c1 = c2 - c1
        c1 = c1 + c1
        Output('z', c1)
    return program

def testPtSub(numChips):
    program = CinnamonProgram('PtSub',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        p = PlaintextInput('x', scale, 0)
        c1 = CiphertextInput('y', scale, 0)
        c1 = p - c1
        Output('z', c1)
    return program

def testPtMul(numChips):
    program = CinnamonProgram('PtMul',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        c1 = CiphertextInput('x', scale, 0)
        c2 = PlaintextInput('y', scale, 0)
        c1 = c1 * c2
        c1 = c1 + c1
        Output('z', c1)
    return program

def testRotate(numChips):
    program = CinnamonProgram('Rotate',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        c1 = CiphertextInput('x', 2 * scale, 0)
        c1 = c1 >> 2
        Output('z', c1)
    return program

def testMulRotate(numChips):
    program = CinnamonProgram('MulRotate',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        c1 = CiphertextInput('x', scale, level)
        c2 = CiphertextInput('y', scale, level)
        c3 = c1 * c2
        c3 = c3.relinearize()
        c3 = c3 << 2
        c3 = c3.rescale()
        Output('z', c3)
    return program

def testMul(numChips):
    program = CinnamonProgram('Mul',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        c1 = CiphertextInput('x', scale, level)
        c2 = CiphertextInput('y', scale, level)
        c3 = c1 * c2
        c3 = c3.relinearize()
        c3 = c3.rescale()
        Output('z', c3)
    return program

def testMulRescale(numChips):
    program = CinnamonProgram('MulRescale',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        c1 = CiphertextInput('x', scale, level)
        c2 = CiphertextInput('y', scale, level)
        c3 = c1 * c2
        c3 = c3.relinearize()
        c3 = c3.rescale()
        Output('z', c3)
    return program

def testMulAdd(numChips):
    program = CinnamonProgram('MulAdd',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        c1 = CiphertextInput('x', scale, level)
        c2 = CiphertextInput('y', scale, level)
        a = PlaintextInput('a', scale * 2, level)
        c3 = c1 * c2
        c4 = c1 * c2
        c3 = c3 + c4
        c3 = c3.relinearize()
        c3 = a + c3
        c3 = c3.rescale()
        Output('z', c3)
    return program

def testRelin(numChips):
    program = CinnamonProgram('MulAdd',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        c1 = CiphertextInput('x', 2 * scale, level)
        c1 = c1.rescale()
        Output('z', c1)
    return program

def testRelin2(numChips):
    program = CinnamonProgram('MulAdd',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        c1 = CiphertextInput('x', scale, level)
        c2 = CiphertextInput('y', scale, level)
        c4 = c1 * c2
        c4 = c4.relinearize2()
        Output('z0', c4)
    return program

def testMulAdd2(numChips):
    program = CinnamonProgram('MulAdd',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        c1 = CiphertextInput('x', scale, level)
        c2 = CiphertextInput('y', scale, level)
        c3 = c1 * c2
        c3 = c3.relinearize()
        c4 = c1 * c2
        c3 = c3 + c4
        c3 = c3.relinearize()
        c3 = c3.rescale()
        Output('z', c3)
    return program

def testBootstrapModRaise(numChips):
    program = CinnamonProgram('BootstrapModRaise',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        TopLevel = 10
        c1 = CiphertextInput('xx', scale, 5 - 2)
        c1 = c1.bootstrapModRaise()
        Output('z', c1)
    return program

def testModSwitchPt(numChips):
    program = CinnamonProgram('ModSwitchPt',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        level = 0
        c1 = CiphertextInput('x', scale, level + 1)
        p1 = PlaintextInput('p', scale, level)
        p1 = p1.modswitch()
        c1 = c1 * p1
        Output('z', c1)
    return program

def testRotMulAdd(numChips):
    program = CinnamonProgram('RotMulAdd',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        doubleScale = 2 * scale
        level = 0
        c1 = CiphertextInput('x', doubleScale, level)
        for _ in range(1):
            c1 = c1 >> 1
        Output('z', c1)
    return program

def testRescale(numChips):
    program = CinnamonProgram('Rescale',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        doubleScale = 2 * scale
        level = 0
        c1 = CiphertextInput('x', 2 * scale, level)
        c1 = c1.rescale()
        Output('z', c1)
    return program

def testPtMulRescale(numChips):
    program = CinnamonProgram('PtMulRescale',RNS_BIT_SIZE,numChips)
    with program:
        scale = 28
        doubleScale = 2 * scale
        level = 0
        c1 = CiphertextInput('x', scale, level)
        p = PlaintextInput('y', scale, level)
        c1 = c1 * p
        c1 = c1.rescale()
        Output('z', c1)
    return program

from cinnamon.compiler import cinnamon_compile

topLevel=51
numChips=1
numVregs=1000

# program, testcase = testMulAdd(numChips), "mul_add"
# program, testcase = testMulAdd2(numChips), "mul_add2"
# program, testcase = testPtSub(numChips), "ptsub"
# program, testcase = testRelin2(numChips), "relin2"
# program, testcase = testRotate(numChips), "rotate"
program, testcase = testMulRotate(numChips), "mul_rot"
# program, testcase = testAddScalar(numChips), "add_scalar"
# program, testcase = testMul(numChips), "mul"
# program, testcase = testPtMul(numChips), "ptmul"
# program, testcase = testAdd(numChips), "add"
# program, testcase = testRelin(numChips), "relin"
# program, testcase = testRescale(numChips), "rescale"
# program, testcase = testRotate(numChips), "rotate"

outputDir=f"outputs/{testcase}_{numChips}"
import os
os.system(f'mkdir -p {outputDir}')

cinnamon_compile(program,topLevel,numChips,numVregs,f"{outputDir}/")
