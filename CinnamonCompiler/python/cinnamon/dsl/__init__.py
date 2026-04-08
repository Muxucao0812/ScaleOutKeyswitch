# Copyright (c) Siddharth Jayashankar. All rights reserved.
# Licensed under the MIT license.
from .. import *

_current_program = None
def _curr():
    """ Returns the CinnamonProgram that is currently in context """
    global _current_program
    if _current_program == None:
        raise RuntimeError("No Program in context")
    return _current_program

def _get_term(x, program):
    """ Maps supported types into terms """
    if isinstance(x, Expression):
        return x.term
    elif isinstance(x, Term):
        return x
    else:
        raise TypeError("No conversion to Term available for " + str(x))

class Expression():
    """ Wrapper for Cinnamon's Term class. Provides operator overloads that
        create terms in the associated CinnamonProgram.
        
        Attributes
        ----------
        term
            The native term
        program :
            The program the wrapped term is in
        """

    def __init__(self, term, program):
        self.term = term
        self.program = program

    def __add__(self,other):
        """ Create a new addition term """
        return Expression(self.program._make_add(self.term, _get_term(other, self.program)), self.program)

    def __radd__(self,other):
        """ Create a new addition term """
        return Expression(self.program._make_add(_get_term(other, self.program), self.term), self.program)

    def __sub__(self,other):
        """ Create a new subtraction term """
        return Expression(self.program._make_subtract(self.term, _get_term(other, self.program)), self.program)

    def __rsub__(self,other):
        """ Create a new subtraction term """
        return Expression(self.program._make_subtract(_get_term(other, self.program), self.term), self.program)

    def __mul__(self,other):
        """ Create a new multiplication term """
        return Expression(self.program._make_multiply(self.term, _get_term(other, self.program)), self.program)

    def __rmul__(self,other):
        """ Create a new multiplication term """
        return Expression(self.program._make_multiply(_get_term(other, self.program), self.term), self.program)

    def __lshift__(self,rotation):
        """ Create a left rotation term """
        return Expression(self.program._make_left_rotation(self.term, rotation), self.program)

    def __rshift__(self,rotation):
        """ Create a right rotation term """
        return Expression(self.program._make_right_rotation(self.term, rotation), self.program)
    
    def __neg__(self):
        """ Create a negation term """
        return Expression(self.program._make_negate(self.term), self.program)

    def rescale(self):
        """ Create a rescale term """
        return Expression(self.program._make_rescale(self.term), self.program)

    def relinearize(self):
        """ Create a reliniearization term """
        return Expression(self.program._make_relinearize(self.term), self.program)

    def relinearize2(self):
        """ Create a reliniearization term """
        return Expression(self.program._make_relinearize2(self.term), self.program)

    def toEphemeral(self):
        """ Create a reliniearization term """
        return Expression(self.program._make_ephemeral(self.term), self.program)

    def modswitch(self):
        """ Create a reliniearization term """
        return Expression(self.program._make_modswitch(self.term), self.program)

    def bootstrapModRaise(self, newLevel=0):
        """ Create a bootstrap modraise term """
        return Expression(self.program._make_bootstrap_modraise(self.term, newLevel), self.program)

    def conjugate(self):
        """ Create a conjugation term """
        return Expression(self.program._make_conjugate(self.term), self.program)
    
    def level(self):
        """ Level of this term """
        return self.term.level

    def scale(self):
        """ Scale of this term """
        return self.term.scale

class CinnamonProgram(Program):
    """ A wrapper for the native Program class. Acts as a context manager to
        set the program the Input and Output free functions operate on. """

    def __init__(self, name, rns_bit_size=28, num_chips=1):
        """ Create a new CinnamonProgram with a name and a vector size
            
            Parameters
            ----------
            name : str
                The name of the program
            rns_bit_size: int
                The size in bits of the RNS primes
            num_chips: int
                The number of chips to be compiled for
            """
        super().__init__(name, rns_bit_size, num_chips)

    def __enter__(self):
        global _current_program
        if _current_program != None:
            raise RuntimeError("There is already a Program in context")
        _current_program = self
    
    def __exit__(self, exc_type, exc_value, exc_traceback):
        global _current_program
        if _current_program != self:
            raise RuntimeError("This program is not currently in context")
        _current_program = None

def PlaintextInput(name, scale, level, scalar=False):
    """ Create a new named input term in the current CinnamonProgram

        Parameters
        ----------
        name : str
            The name of the input
        scale: int
            The scale of the input
        level: int 
            The level of the input
        scale: bool
            Whether the input is a scalar value
        """
    program = _curr()
    return Expression(program._make_plaintext_input(name, scale, level, scalar), program)

def CiphertextInput(name, scale, level):
    """ Create a new named input term in the current CinnamonProgram

        Parameters
        ----------
        name : str
            The name of the input
        scale: int 
            The scale of the input
        level: int
            The level of the input
        """
    program = _curr()
    return Expression(program._make_ciphertext_input(name, scale, level), program)

def Receive(term):
    program = _curr()
    """ Create a new receive term """
    return Expression(program._make_receive(_get_term(term, program)), program)

def Partition(size, id):
    """ Create a new partition in the current CinnamonProgram"""
    program = _curr()
    return program._make_partition(size,id)

def CurrentPartitionSize():
    program = _curr()
    return program._currentPartitionSize()

def CurrentPartitionID():
    program = _curr()
    return program._currentPartitionID()

def CinnamonStream(StreamSize,NumStreams,StreamFn,*argv,**kwargs):
    """ Create Parallel Execution Streams """
    currentPartitionSize = CurrentPartitionSize()
    currentPartitionID = CurrentPartitionID()
    if StreamSize < 1:
        raise Exception("Stream size must be greater than or equal to 1")
    if StreamSize > currentPartitionSize:
        raise Exception(f"Stream size must be less than currentPartitionSize={currentPartitionSize}")
    
    numParallelStreams = currentPartitionSize // StreamSize
    for sid in range(NumStreams):
        Partition(StreamSize,currentPartitionID*numParallelStreams + (sid % numParallelStreams))
        StreamFn(sid,*argv,**kwargs)
    Partition(currentPartitionSize,currentPartitionID)


def RotateMultiplyAccumulate(ciphertext, plaintexts, rotations):
    """ Create a Rotate Multiply Accumulate term """
    program = _curr()
    ciphertextTerm = _get_term(ciphertext, program)
    plaintextTerms = [_get_term(plaintext, program) for plaintext in plaintexts]
    return Expression(program._make_rotate_multiply_accumulate(ciphertextTerm, plaintextTerms, rotations), program)

def MultiplyRotateAccumulate(ciphertext, plaintexts, rotations):
    """ Create a Multiply Rotate Accumulate term """
    program = _curr()
    ciphertextTerm = _get_term(ciphertext, program)
    plaintextTerms = [_get_term(plaintext, program) for plaintext in plaintexts]
    return Expression(program._make_multiply_rotate_accumulate(ciphertextTerm, plaintextTerms, rotations), program)

def RotateAccumulate(ciphertext, rotations):
    """ Create a Rotate Accumulate term """
    program = _curr()
    ciphertextTerm = _get_term(ciphertext, program)
    return Expression(program._make_rotate_accumulate(ciphertextTerm, rotations), program)

def BsgsMultiplyAccumulate(ciphertext, plaintexts, babySteprotations, giantStepRotations):
    """ Create a Baby Step Giant Step Multiply Accumulate term """
    program = _curr()
    ciphertextTerm = _get_term(ciphertext, program)
    plaintextTerms = [_get_term(plaintext, program) for plaintext in plaintexts]
    return Expression(program._make_bsgs_multiply_accumulate(ciphertextTerm, plaintextTerms, babySteprotations, giantStepRotations), program)

def Output(name, expr):
    """ Create a new named output term in the current CinnamonProgram

        Parameters
        ----------
        name : str
            The name of the output
        """
    program = _curr()
    program._make_output(name, _get_term(expr, program))
