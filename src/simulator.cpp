#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <iomanip>

using namespace std;

const int MEMORY_SIZE = 1024 * 1024; // 1 MB
const int REG_COUNT = 32;
const uint32_t STACK_START = 0x80000; // 512KB for stack

class RISCVSimulator {
private:
    uint32_t regs[REG_COUNT];
    uint8_t memory[MEMORY_SIZE];
    uint32_t pc;
    bool running;
    int return_value;

    // Sign extend functions
    int32_t sign_extend(uint32_t value, int bits) {
        uint32_t sign_bit = 1 << (bits - 1);
        if (value & sign_bit) {
            return value | (~((1 << bits) - 1));
        }
        return value;
    }

    // Memory access functions
    uint32_t read_word(uint32_t addr) {
        if (addr + 3 >= MEMORY_SIZE) return 0;
        return *(uint32_t*)(&memory[addr]);
    }

    void write_word(uint32_t addr, uint32_t value) {
        if (addr + 3 >= MEMORY_SIZE) return;
        *(uint32_t*)(&memory[addr]) = value;
    }

    uint16_t read_halfword(uint32_t addr) {
        if (addr + 1 >= MEMORY_SIZE) return 0;
        return *(uint16_t*)(&memory[addr]);
    }

    void write_halfword(uint32_t addr, uint16_t value) {
        if (addr + 1 >= MEMORY_SIZE) return;
        *(uint16_t*)(&memory[addr]) = value;
    }

    uint8_t read_byte(uint32_t addr) {
        if (addr >= MEMORY_SIZE) return 0;
        return memory[addr];
    }

    void write_byte(uint32_t addr, uint8_t value) {
        if (addr >= MEMORY_SIZE) return;
        memory[addr] = value;
    }

    // Decode instruction fields
    uint32_t get_opcode(uint32_t inst) { return inst & 0x7F; }
    uint32_t get_rd(uint32_t inst) { return (inst >> 7) & 0x1F; }
    uint32_t get_funct3(uint32_t inst) { return (inst >> 12) & 0x7; }
    uint32_t get_rs1(uint32_t inst) { return (inst >> 15) & 0x1F; }
    uint32_t get_rs2(uint32_t inst) { return (inst >> 20) & 0x1F; }
    uint32_t get_funct7(uint32_t inst) { return (inst >> 25) & 0x7F; }

    // Immediate extraction
    int32_t get_imm_I(uint32_t inst) {
        return sign_extend(inst >> 20, 12);
    }

    int32_t get_imm_S(uint32_t inst) {
        uint32_t imm = ((inst >> 7) & 0x1F) | ((inst >> 25) << 5);
        return sign_extend(imm, 12);
    }

    int32_t get_imm_B(uint32_t inst) {
        uint32_t imm = ((inst >> 8) & 0xF) << 1 |
                      ((inst >> 25) & 0x3F) << 5 |
                      ((inst >> 7) & 0x1) << 11 |
                      ((inst >> 31) & 0x1) << 12;
        return sign_extend(imm, 13);
    }

    int32_t get_imm_U(uint32_t inst) {
        return inst & 0xFFFFF000;
    }

    int32_t get_imm_J(uint32_t inst) {
        uint32_t imm = ((inst >> 21) & 0x3FF) << 1 |
                      ((inst >> 20) & 0x1) << 11 |
                      ((inst >> 12) & 0xFF) << 12 |
                      ((inst >> 31) & 0x1) << 20;
        return sign_extend(imm, 21);
    }

    void execute_instruction(uint32_t inst) {
        uint32_t opcode = get_opcode(inst);
        uint32_t rd = get_rd(inst);
        uint32_t rs1 = get_rs1(inst);
        uint32_t rs2 = get_rs2(inst);
        uint32_t funct3 = get_funct3(inst);
        uint32_t funct7 = get_funct7(inst);

        switch (opcode) {
            case 0x37: // LUI
                regs[rd] = get_imm_U(inst);
                break;

            case 0x17: // AUIPC
                regs[rd] = pc + get_imm_U(inst);
                break;

            case 0x6F: { // JAL
                regs[rd] = pc + 4;
                pc += get_imm_J(inst);
                return;
            }

            case 0x67: { // JALR
                uint32_t target = (regs[rs1] + get_imm_I(inst)) & ~1;
                regs[rd] = pc + 4;
                pc = target;
                return;
            }

            case 0x63: { // Branch instructions
                int32_t offset = get_imm_B(inst);
                bool branch = false;
                switch (funct3) {
                    case 0x0: branch = (regs[rs1] == regs[rs2]); break; // BEQ
                    case 0x1: branch = (regs[rs1] != regs[rs2]); break; // BNE
                    case 0x4: branch = ((int32_t)regs[rs1] < (int32_t)regs[rs2]); break; // BLT
                    case 0x5: branch = ((int32_t)regs[rs1] >= (int32_t)regs[rs2]); break; // BGE
                    case 0x6: branch = (regs[rs1] < regs[rs2]); break; // BLTU
                    case 0x7: branch = (regs[rs1] >= regs[rs2]); break; // BGEU
                }
                if (branch) {
                    pc += offset;
                    return;
                }
                break;
            }

            case 0x03: { // Load instructions
                uint32_t addr = regs[rs1] + get_imm_I(inst);
                switch (funct3) {
                    case 0x0: regs[rd] = sign_extend(read_byte(addr), 8); break; // LB
                    case 0x1: regs[rd] = sign_extend(read_halfword(addr), 16); break; // LH
                    case 0x2: regs[rd] = read_word(addr); break; // LW
                    case 0x4: regs[rd] = read_byte(addr); break; // LBU
                    case 0x5: regs[rd] = read_halfword(addr); break; // LHU
                }
                break;
            }

            case 0x23: { // Store instructions
                uint32_t addr = regs[rs1] + get_imm_S(inst);
                switch (funct3) {
                    case 0x0: write_byte(addr, regs[rs2]); break; // SB
                    case 0x1: write_halfword(addr, regs[rs2]); break; // SH
                    case 0x2: write_word(addr, regs[rs2]); break; // SW
                }
                break;
            }

            case 0x13: { // Immediate ALU operations
                int32_t imm = get_imm_I(inst);
                switch (funct3) {
                    case 0x0: regs[rd] = regs[rs1] + imm; break; // ADDI
                    case 0x2: regs[rd] = ((int32_t)regs[rs1] < imm) ? 1 : 0; break; // SLTI
                    case 0x3: regs[rd] = (regs[rs1] < (uint32_t)imm) ? 1 : 0; break; // SLTIU
                    case 0x4: regs[rd] = regs[rs1] ^ imm; break; // XORI
                    case 0x6: regs[rd] = regs[rs1] | imm; break; // ORI
                    case 0x7: regs[rd] = regs[rs1] & imm; break; // ANDI
                    case 0x1: regs[rd] = regs[rs1] << (imm & 0x1F); break; // SLLI
                    case 0x5:
                        if (funct7 == 0x00)
                            regs[rd] = regs[rs1] >> (imm & 0x1F); // SRLI
                        else
                            regs[rd] = (int32_t)regs[rs1] >> (imm & 0x1F); // SRAI
                        break;
                }
                break;
            }

            case 0x33: { // Register ALU operations
                if (funct7 == 0x01) {
                    // RV32M - Multiply/Divide extension
                    switch (funct3) {
                        case 0x0: { // MUL
                            int64_t result = (int64_t)(int32_t)regs[rs1] * (int64_t)(int32_t)regs[rs2];
                            regs[rd] = (uint32_t)result;
                            break;
                        }
                        case 0x1: { // MULH
                            int64_t result = (int64_t)(int32_t)regs[rs1] * (int64_t)(int32_t)regs[rs2];
                            regs[rd] = (uint32_t)(result >> 32);
                            break;
                        }
                        case 0x2: { // MULHSU
                            int64_t result = (int64_t)(int32_t)regs[rs1] * (uint64_t)regs[rs2];
                            regs[rd] = (uint32_t)(result >> 32);
                            break;
                        }
                        case 0x3: { // MULHU
                            uint64_t result = (uint64_t)regs[rs1] * (uint64_t)regs[rs2];
                            regs[rd] = (uint32_t)(result >> 32);
                            break;
                        }
                        case 0x4: // DIV
                            if (regs[rs2] == 0)
                                regs[rd] = -1;
                            else if (regs[rs1] == 0x80000000 && regs[rs2] == 0xFFFFFFFF)
                                regs[rd] = 0x80000000;
                            else
                                regs[rd] = (int32_t)regs[rs1] / (int32_t)regs[rs2];
                            break;
                        case 0x5: // DIVU
                            if (regs[rs2] == 0)
                                regs[rd] = 0xFFFFFFFF;
                            else
                                regs[rd] = regs[rs1] / regs[rs2];
                            break;
                        case 0x6: // REM
                            if (regs[rs2] == 0)
                                regs[rd] = regs[rs1];
                            else if (regs[rs1] == 0x80000000 && regs[rs2] == 0xFFFFFFFF)
                                regs[rd] = 0;
                            else
                                regs[rd] = (int32_t)regs[rs1] % (int32_t)regs[rs2];
                            break;
                        case 0x7: // REMU
                            if (regs[rs2] == 0)
                                regs[rd] = regs[rs1];
                            else
                                regs[rd] = regs[rs1] % regs[rs2];
                            break;
                    }
                } else {
                    switch (funct3) {
                        case 0x0:
                            if (funct7 == 0x00)
                                regs[rd] = regs[rs1] + regs[rs2]; // ADD
                            else
                                regs[rd] = regs[rs1] - regs[rs2]; // SUB
                            break;
                        case 0x1: regs[rd] = regs[rs1] << (regs[rs2] & 0x1F); break; // SLL
                        case 0x2: regs[rd] = ((int32_t)regs[rs1] < (int32_t)regs[rs2]) ? 1 : 0; break; // SLT
                        case 0x3: regs[rd] = (regs[rs1] < regs[rs2]) ? 1 : 0; break; // SLTU
                        case 0x4: regs[rd] = regs[rs1] ^ regs[rs2]; break; // XOR
                        case 0x5:
                            if (funct7 == 0x00)
                                regs[rd] = regs[rs1] >> (regs[rs2] & 0x1F); // SRL
                            else
                                regs[rd] = (int32_t)regs[rs1] >> (regs[rs2] & 0x1F); // SRA
                            break;
                        case 0x6: regs[rd] = regs[rs1] | regs[rs2]; break; // OR
                        case 0x7: regs[rd] = regs[rs1] & regs[rs2]; break; // AND
                    }
                }
                break;
            }

            case 0x0F: // FENCE
                // NOP for our purposes
                break;

            case 0x73: // ECALL/EBREAK
                if (inst == 0x00000073) { // ECALL
                    // System call - stop simulation and save return value
                    return_value = (int32_t)regs[10]; // a0 register
                    running = false;
                    return;
                } else if (inst == 0x00100073) { // EBREAK
                    running = false;
                    return;
                }
                break;

            default:
                // Unknown instruction - continue
                break;
        }

        regs[0] = 0; // x0 is always 0
        pc += 4;
    }

public:
    RISCVSimulator() {
        memset(regs, 0, sizeof(regs));
        memset(memory, 0, sizeof(memory));
        pc = 0;
        running = true;
        return_value = 0;
        // Initialize stack pointer to high memory
        regs[2] = STACK_START; // sp (x2)
    }

    void load_program(const vector<uint8_t>& data) {
        size_t size = min(data.size(), (size_t)MEMORY_SIZE);
        memcpy(memory, data.data(), size);
    }

    void run() {
        int max_instructions = 100000000; // 100M instructions max
        int inst_count = 0;
        while (running && pc < MEMORY_SIZE - 3 && inst_count < max_instructions) {
            uint32_t inst = read_word(pc);
            if (inst == 0 && pc == 0) {
                // Empty program or uninitialized memory
                break;
            }
            execute_instruction(inst);
            inst_count++;
        }
    }

    uint32_t get_reg(int idx) {
        return regs[idx];
    }

    void print_output() {
        // Print return value from program execution
        cout << return_value << endl;
    }
};

int main() {
    vector<uint8_t> data;

    // Read input data
    uint8_t byte;
    while (cin.read(reinterpret_cast<char*>(&byte), 1)) {
        data.push_back(byte);
    }

    RISCVSimulator sim;
    sim.load_program(data);
    sim.run();
    sim.print_output();

    return 0;
}
