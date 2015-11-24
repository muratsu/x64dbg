/**
 @file disasm_helper.cpp

 @brief Implements the disasm helper class.
 */

#include "disasm_helper.h"
#include "value.h"
#include "console.h"
#include "memory.h"
#include <capstone_wrapper.h>

duint disasmback(unsigned char* data, duint base, duint size, duint ip, int n)
{
    int i;
    duint abuf[131], addr, back, cmdsize;
    unsigned char* pdata;

    // Reset Disasm Structure
    Capstone cp;

    // Check if the pointer is not null
    if(data == NULL)
        return 0;

    // Round the number of back instructions to 127
    if(n < 0)
        n = 0;
    else if(n > 127)
        n = 127;

    // Check if the instruction pointer ip is not outside the memory range
    if(ip >= size)
        ip = size - 1;

    // Obvious answer
    if(n == 0)
        return ip;

    if(ip < (duint)n)
        return ip;

    back = MAX_DISASM_BUFFER * (n + 3); // Instruction length limited to 16

    if(ip < back)
        back = ip;

    addr = ip - back;

    pdata = data + addr;

    for(i = 0; addr < ip; i++)
    {
        abuf[i % 128] = addr;

        if(!cp.Disassemble(0, pdata, (int)size))
            cmdsize = 1;
        else
            cmdsize = cp.Size();

        pdata += cmdsize;
        addr += cmdsize;
        back -= cmdsize;
        size -= cmdsize;
    }

    if(i < n)
        return abuf[0];
    else
        return abuf[(i - n + 128) % 128];
}

duint disasmnext(unsigned char* data, duint base, duint size, duint ip, int n)
{
    int i;
    duint cmdsize;
    unsigned char* pdata;

    // Reset Disasm Structure
    Capstone cp;

    if(data == NULL)
        return 0;

    if(ip >= size)
        ip = size - 1;

    if(n <= 0)
        return ip;

    pdata = data + ip;
    size -= ip;

    for(i = 0; i < n && size > 0; i++)
    {
        if(!cp.Disassemble(0, pdata, (int)size))
            cmdsize = 1;
        else
            cmdsize = cp.Size();

        pdata += cmdsize;
        ip += cmdsize;
        size -= cmdsize;
    }

    return ip;
}

const char* disasmtext(duint addr)
{
    unsigned char buffer[MAX_DISASM_BUFFER] = "";
    DbgMemRead(addr, buffer, sizeof(buffer));
    Capstone cp;
    static char instruction[64] = "";
    if(!cp.Disassemble(addr, buffer))
        strcpy_s(instruction, "???");
    else
        sprintf_s(instruction, "%s %s", cp.GetInstr()->mnemonic, cp.GetInstr()->op_str);
    return instruction;
}

static void HandleCapstoneOperand(Capstone & cp, int opindex, DISASM_ARG* arg)
{
    const cs_x86 & x86 = cp.x86();
    const cs_x86_op & op = x86.operands[opindex];
    arg->segment = SEG_DEFAULT;
    strcpy_s(arg->mnemonic, cp.OperandText(opindex).c_str());
    switch(op.type)
    {
    case X86_OP_REG:
    {
        const char* regname = cp.RegName((x86_reg)op.reg);
        arg->type = arg_normal;
        duint value;
        if(!valfromstring(regname, &value, true, true))
            value = 0;
        arg->constant = arg->value = value;
    }
    break;

    case X86_OP_IMM:
    {
        arg->type = arg_normal;
        arg->constant = arg->value = (duint)op.imm;
    }
    break;

    case X86_OP_MEM:
    {
        arg->type = arg_memory;
        const x86_op_mem & mem = op.mem;
        if(mem.base == X86_REG_RIP)  //rip-relative
            arg->constant = cp.Address() + (duint)mem.disp + cp.Size();
        else
            arg->constant = (duint)mem.disp;
        duint value;
        if(!valfromstring(arg->mnemonic, &value, true, true))
            return;
        arg->value = value;
        if(DbgMemIsValidReadPtr(value))
        {
            switch(op.size)
            {
            case 1:
                DbgMemRead(value, (unsigned char*)&arg->memvalue, 1);
                break;
            case 2:
                DbgMemRead(value, (unsigned char*)&arg->memvalue, 2);
                break;
            case 4:
                DbgMemRead(value, (unsigned char*)&arg->memvalue, 4);
                break;
            case 8:
                DbgMemRead(value, (unsigned char*)&arg->memvalue, 8);
                break;
            }
        }
    }
    break;
    }
}

void disasmget(unsigned char* buffer, duint addr, DISASM_INSTR* instr)
{
    if(!DbgIsDebugging())
    {
        if(instr)
            instr->argcount = 0;
        return;
    }
    memset(instr, 0, sizeof(DISASM_INSTR));
    Capstone cp;
    if(!cp.Disassemble(addr, buffer, MAX_DISASM_BUFFER))
    {
        strcpy_s(instr->instruction, "???");
        instr->instr_size = 1;
        instr->type = instr_normal;
        instr->argcount = 0;
        return;
    }
    const cs_insn* cpInstr = cp.GetInstr();
    sprintf_s(instr->instruction, "%s %s", cpInstr->mnemonic, cpInstr->op_str);
    instr->instr_size = cpInstr->size;
    if(cp.InGroup(CS_GRP_JUMP) || cp.IsLoop() || cp.InGroup(CS_GRP_RET) || cp.InGroup(CS_GRP_CALL))
        instr->type = instr_branch;
    else if(strstr(cpInstr->op_str, "sp") || strstr(cpInstr->op_str, "bp"))
        instr->type = instr_stack;
    else
        instr->type = instr_normal;
    instr->argcount = cp.x86().op_count <= 3 ? cp.x86().op_count : 3;
    for(int i = 0; i < instr->argcount; i++)
        HandleCapstoneOperand(cp, i, &instr->arg[i]);
}

void disasmget(duint addr, DISASM_INSTR* instr)
{
    if(!DbgIsDebugging())
    {
        if(instr)
            instr->argcount = 0;
        return;
    }
    unsigned char buffer[MAX_DISASM_BUFFER] = "";
    DbgMemRead(addr, buffer, sizeof(buffer));
    disasmget(buffer, addr, instr);
}

void disasmprint(duint addr)
{
    DISASM_INSTR instr;
    memset(&instr, 0, sizeof(instr));
    disasmget(addr, &instr);
    dprintf(">%d:\"%s\":\n", instr.type, instr.instruction);
    for(int i = 0; i < instr.argcount; i++)
        dprintf(" %d:%d:%" fext "X:%" fext "X:%" fext "X\n", i, instr.arg[i].type, instr.arg[i].constant, instr.arg[i].value, instr.arg[i].memvalue);
}

static bool isasciistring(const unsigned char* data, int maxlen)
{
    int len = 0;
    for(char* p = (char*)data; *p; len++, p++)
    {
        if(len >= maxlen)
            break;
    }

    if(len < 2 || len + 1 >= maxlen)
        return false;
    for(int i = 0; i < len; i++)
        if(!isprint(data[i]) && !isspace(data[i]))
            return false;
    return true;
}

static bool isunicodestring(const unsigned char* data, int maxlen)
{
    int len = 0;
    for(wchar_t* p = (wchar_t*)data; *p; len++, p++)
    {
        if(len >= maxlen)
            break;
    }

    if(len < 2 || len + 1 >= maxlen)
        return false;
    for(int i = 0; i < len * 2; i += 2)
    {
        if(data[i + 1]) //Extended ASCII only
            return false;
        if(!isprint(data[i]) && !isspace(data[i]))
            return false;
    }
    return true;
}

bool disasmispossiblestring(duint addr)
{
    unsigned char data[11];
    memset(data, 0, sizeof(data));
    if(!MemRead(addr, data, sizeof(data) - 3))
        return false;
    duint test = 0;
    memcpy(&test, data, sizeof(duint));
    if(isasciistring(data, sizeof(data)) || isunicodestring(data, _countof(data)))
        return true;
    return false;
}

bool disasmgetstringat(duint addr, STRING_TYPE* type, char* ascii, char* unicode, int maxlen)
{
    if(type)
        *type = str_none;
    if(!disasmispossiblestring(addr))
        return false;
    Memory<unsigned char*> data((maxlen + 1) * 2, "disasmgetstringat:data");
    if(!MemRead(addr, data(), (maxlen + 1) * 2))
        return false;

    // Save a few pointer casts
    auto asciiData = (char*)data();
    auto unicodeData = (wchar_t*)data();

    // First check if this was an ASCII only string
    if(isasciistring(data(), maxlen))
    {
        if(type)
            *type = str_ascii;

        // Escape the string
        String escaped = StringUtils::Escape(asciiData);

        // Copy data back to outgoing parameter
        strncpy_s(ascii, min(int(escaped.length()) + 1, maxlen), escaped.c_str(), _TRUNCATE);
        return true;
    }

    if(isunicodestring(data(), maxlen))
    {
        if(type)
            *type = str_unicode;

        // Determine string length only once, limited to output buffer size
        int unicodeLength = min(int(wcslen(unicodeData)), maxlen);

        // Truncate each wchar_t to char
        for(int i = 0; i < unicodeLength; i++)
            asciiData[i] = char(unicodeData[i] & 0xFF);

        // Fix the null terminator (data len = maxlen + 1)
        asciiData[unicodeLength] = '\0';

        // Escape the string
        String escaped = StringUtils::Escape(asciiData);

        // Copy data back to outgoing parameter
        strncpy_s(unicode, min(int(escaped.length()) + 1, maxlen), escaped.c_str(), _TRUNCATE);
        return true;
    }

    return false;
}

int disasmgetsize(duint addr, unsigned char* data)
{
    Capstone cp;
    if(!cp.Disassemble(addr, data, MAX_DISASM_BUFFER))
        return 1;
    return cp.Size();
}

int disasmgetsize(duint addr)
{
    char data[MAX_DISASM_BUFFER];
    if(!MemRead(addr, data, sizeof(data)))
        return 1;
    return disasmgetsize(addr, (unsigned char*)data);
}