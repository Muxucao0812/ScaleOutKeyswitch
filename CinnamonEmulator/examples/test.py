import argparse
import cinnamon_emulator

Primes = [204865537, 205651969, 206307329, 207880193, 209059841, 210370561, 211025921, 211812353, 214171649, 215482369, 215744513, 216137729, 216924161, 217317377, 218628097, 219676673, 220594177, 221249537, 222035969, 222167041, 222953473, 223215617, 224002049, 224133121, 225574913, 228065281, 228458497, 228720641, 230424577, 230686721, 230817793, 231473153, 232390657, 232652801, 234356737, 235798529, 236584961, 236716033, 239337473, 239861761, 240648193, 241827841, 244842497, 244973569, 245235713, 245760001, 246415361, 249561089, 253100033, 253493249, 254279681, 256376833, 256770049, 257949697, 258605057, 260571137, 260702209, 261488641, 261881857, 263323649, 263454721, 264634369, 265420801, 268042241]

def main(inputs_file,instructions_file):
    Slots = 64 
    arr = [i for i in range(Slots)]
    Scale = 1 << 28

    context = cinnamon_emulator.Context(Slots,Primes)

    secretKey = [0]*(2*Slots)
    for i in range(0,2*Slots,4):
        secretKey[i+2] = 1
        secretKey[i+3] = -1

    random_seed = [0,1,2,3,4,5,6,7]
    encryptor = cinnamon_emulator.CKKSEncryptor(context,secretKey,random_seed)
    emulator = cinnamon_emulator.Emulator(context)

    RawInputs = {}
    OutScale = {}

    Level = 51
    RawInputs["x"] = (arr,Scale)
    RawInputs["y"] = (3,Primes[Level-1])
    OutScale["z"] = Scale

    emulator.generate_and_serialize_evalkeys("evalkeys","program_inputs",encryptor)
    emulator.generate_inputs("program_inputs","evalkeys",RawInputs,encryptor)
    emulator.run_program("instructions",1,1024)
    emulator.decrypt_and_print_outputs(encryptor,OutScale)

if __name__=="__main__": 
    parser = argparse.ArgumentParser()
    parser.add_argument('--instructions_file', type=str, default="instructions")
    parser.add_argument('--inputs_file', type=str, default="program_inputs")

    args = parser.parse_args()
    main(args.instructions_file,args.inputs_file)
