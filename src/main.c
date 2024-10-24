#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/select.h>
#include <sys/time.h>

#include <curses.h>

#include "sprites.h"

// #define DEBUG

#ifdef DEBUG
#define DEBUG_MSG(...) printf(__VA_ARGS__)
#else
#define DEBUG_MSG(msg, ...)
#define DISPLAY
#endif

#define CHIP8_DISPLAY_WIDTH 64
#define CHIP8_DISPLAY_HEIGHT 32

#define RENDER_CHARACTER "@@"

#define CHIP8_STACK_SIZE 16
#define CHIP8_MEMORY_SIZE 4096
#define CHIP8_REGISTER_SIZE 16

struct chip8_memory {
    uint16_t pc;
    uint16_t sp;
    int16_t i;

    uint8_t registers[CHIP8_REGISTER_SIZE];
    uint16_t stack[CHIP8_STACK_SIZE];
    uint8_t memory[CHIP8_MEMORY_SIZE];

    uint8_t display_memory[CHIP8_DISPLAY_WIDTH * CHIP8_DISPLAY_HEIGHT];

    uint8_t delay_timer;
    uint8_t sound_timer;
};
typedef struct chip8_memory* chip8_memory_T;

struct timer {
    struct timeval current;
    struct timeval previous;
};
typedef struct timer timer_T;

uint8_t update_timer(timer_T* timer, float clock)
{
    gettimeofday(&(timer->current), NULL);
    if (timer->current.tv_usec < timer->previous.tv_usec)
    {
        timer->previous = timer->current;
        return 1;
    }

    if (timer->current.tv_sec > timer->previous.tv_sec)
    {
        timer->previous = timer->current;
        return 1;
    }

    if ((timer->current.tv_usec - timer->previous.tv_usec) > clock)
    {
        timer->previous = timer->current;
        return 1;
    }

    return 0;
}

float get_timer_delta(timer_T* timer)
{
    return (timer->current.tv_usec - timer->previous.tv_usec) / 1000000.0;
}

int nibbleToSprite(uint8_t v) { return 5 * (v & 0xF); }

chip8_memory_T init_memory()
{
    chip8_memory_T memory = (chip8_memory_T)malloc(sizeof(struct chip8_memory));

    memory->pc = 0x200;
    memory->sp = 0;

    memset(memory->memory, 0, CHIP8_MEMORY_SIZE);
    for (int i = 0; i < 16; i++)
    {
        int location = nibbleToSprite(i);
        for (int j = 0; j < 5; j++)
        {
            memory->memory[location + j] = fonts[i][j];
        }
    }

    memset(memory->stack, 0, CHIP8_STACK_SIZE);
    memset(memory->display_memory, 0, CHIP8_DISPLAY_HEIGHT * CHIP8_DISPLAY_WIDTH);

    return memory;
}

uint8_t* read_rom(char* file, int* byte_count)
{
    FILE* ptr;
    ptr = fopen(file, "rb");

    fseek(ptr, 0, SEEK_END);
    uint32_t rom_size = ftell(ptr);
    fseek(ptr, 0, SEEK_SET);

    uint8_t* bytes = (uint8_t*)malloc(sizeof(uint8_t) * rom_size);

    *byte_count = fread(bytes, sizeof(uint8_t), rom_size, ptr);
    fclose(ptr);

    return bytes;
}

void advance_pc(chip8_memory_T memory) { memory->pc += 2; }

uint16_t get_next_instruction(chip8_memory_T memory)
{
    uint16_t instruction = 0;
    instruction |= (memory->memory[memory->pc] << 8);
    instruction |= (memory->memory[memory->pc + 1]);
    advance_pc(memory);

    return instruction;
}

uint8_t characterToMapping(uint8_t c)
{
    switch (c)
    {
    case '1':
        return 0x1;
    case '2':
        return 0x2;
    case '3':
        return 0x3;
    case '4':
        return 0xC;

    case 'q':
        return 0x4;
    case 'w':
        return 0x5;
    case 'e':
        return 0x6;
    case 'r':
        return 0xD;

    case 'a':
        return 0x7;
    case 's':
        return 0x8;
    case 'd':
        return 0x9;
    case 'f':
        return 0xE;

    case 'z':
        return 0xA;
    case 'x':
        return 0x0;
    case 'c':
        return 0xB;
    case 'v':
        return 0xF;
    default:
        return 0x10;
    }
}

char currentKeyPress()
{
    static int count = 0;
    static int stored = 0;
    int c = getch();
    if (c == ERR)
    {
        count++;

        if (count >= 50)
        {
            count = 0;
            stored = 0x10;
        }

        return stored;
    }
    else
    {
        count = 0;
        stored = characterToMapping(c);
        return stored;
    }
}

int immedateKeyPress() { return characterToMapping(getch()); }

void execute(uint16_t instruction, struct chip8_memory* memory)
{
    uint8_t opcode = (instruction >> 12) & 0x000F;
    uint8_t X = (instruction >> 8) & 0x000F;
    uint8_t Y = (instruction >> 4) & 0x000F;
    uint16_t N = (instruction) & 0x000F;
    uint16_t kk = (instruction) & 0x00FF;
    uint16_t nnn = (instruction) & 0x0FFF;

    DEBUG_MSG("0x%04X | ", instruction);
    switch (opcode)
    {
    case 0:
        {
            if (nnn == 0x0E0) // 00E0 - Display
            {
                DEBUG_MSG("00E0 | Clear Screen\n");

                memset(memory->display_memory, 0, 64 * 32);
            }
            else if (nnn == 0x0EE) // 00EE -- RETURN
            {
                DEBUG_MSG("00EE | RETURN 0x%03X\n", memory->stack[memory->sp]);

                memory->pc = memory->stack[memory->sp];
                memory->sp--;
            }
            else
            {
                DEBUG_MSG("Invalid operation\n");
                exit(-1);
            }
            break;
        }

    case 1: // 1NNN - Goto NNN
        {
            DEBUG_MSG("1NNN | GOTO 0x%03X\n", nnn);
            memory->pc = nnn;
            break;
        }

    case 2: // 2NNN - Call NNN
        {
            DEBUG_MSG("2NNN | Call 0x%03X\n", nnn);

            memory->sp++;
            memory->stack[memory->sp] = memory->pc;
            memory->pc = nnn;
            break;
        }

    case 3: // 3XNN - if (Vx == NN)
        {
            DEBUG_MSG("3XNN | V[0x%01X] == 0x%02X\n", X, kk);

            if (memory->registers[X] == kk) advance_pc(memory);
            break;
        }

    case 4: // 4XNN - if (Vx != NN)
        {
            DEBUG_MSG("4XNN | V[0x%01X] != 0x%02X\n", X, kk);

            if (memory->registers[X] != kk) advance_pc(memory);
            break;
        }

    case 5: // 5XY0 - if (Vx == Vy)
        {
            DEBUG_MSG("5XY0 | V[0x%01X] != V[0x%01X]\n", X, Y);

            if (memory->registers[X] == memory->registers[Y]) advance_pc(memory);
            break;
        }

    case 6: // 6XNN - Vx = NN
        {
            DEBUG_MSG("6XNN | V[0x%01X] = 0x%02X\n", X, kk);

            memory->registers[X] = kk;
            break;
        }
    case 7: // 7XNN - Vx += NN
        {
            DEBUG_MSG("7XNN | V[0x%01X] += 0x%02X\n", X, kk);

            memory->registers[X] += kk;
            break;
        }

    case 8:
        {
            switch (N)
            {
            case 0: // 8XY0 - Vx = Vy
                DEBUG_MSG("8XY0 | V[0x%01X] = V[0x%01X]\n", X, Y);

                memory->registers[X] = memory->registers[Y];
                break;
            case 1: // 8XY1 - Vx |= Vy
                DEBUG_MSG("8XY1 | V[0x%01X] |= V[0x%01X]\n", X, Y);

                memory->registers[X] |= memory->registers[Y];
                memory->registers[0xF] = 0;
                break;
            case 2: // 8XY2 - Vx &= Vy
                DEBUG_MSG("8XY2 | V[0x%01X] &= V[0x%01X]\n", X, Y);

                memory->registers[X] &= memory->registers[Y];
                memory->registers[0xF] = 0;
                break;
            case 3: // 8XY3 - Vx ^= Vy
                DEBUG_MSG("8XY3 | V[0x%01X] ^= V[0x%01X]\n", X, Y);

                memory->registers[X] ^= memory->registers[Y];
                memory->registers[0xF] = 0;
                break;
            case 4: // 8XY4 - Vx += Vy
                {
                    DEBUG_MSG("8XY4 | V[0x%01X] += V[0x%01X]\n", X, Y);

                    int16_t before = memory->registers[X];
                    before += memory->registers[Y];
                    memory->registers[X] = before & 0xFF;
                    memory->registers[0xF] = (before > 0xFF);
                    break;
                }
            case 5: // 8XY5 - Vx -= Vy
                {
                    DEBUG_MSG("8XY5 | V[0x%01X] -= V[0x%01X]\n", X, Y);

                    uint8_t rv = (memory->registers[X] >= memory->registers[Y]);
                    memory->registers[X] -= memory->registers[Y];
                    memory->registers[0xF] = rv;
                    break;
                }
            case 6: // 8XY6 - Vx >>= 1
                {
                    DEBUG_MSG("8XY6 | V[0x%01X] >>= 1\n", X);

                    uint8_t rv = (memory->registers[X] & 0x1);
                    memory->registers[X] >>= 1;
                    memory->registers[0xF] = rv;
                    break;
                }
            case 7: // 8XY7 - Vx = Vy - Vx
                {
                    DEBUG_MSG("8XY7 | V[0x%01X] = V[0x%01X] - V[0x%01X]\n", X, Y, X);

                    uint8_t rv = (memory->registers[Y] >= memory->registers[X]);
                    memory->registers[X] = memory->registers[Y] - memory->registers[X];
                    memory->registers[0xF] = rv;
                    break;
                }

            case 0xE: // 8XYE - Vx = vX << 1
                {
                    DEBUG_MSG("8XYE | V[0x%01X] <<= 1\n", X);

                    uint8_t rv = (memory->registers[X] >> 7) & 1;
                    memory->registers[X] <<= 1;
                    memory->registers[0xF] = rv;
                    break;
                }
            default:
                DEBUG_MSG("Invalid operation\n");
                exit(-1);
                break;
            }
            break;
        }

    case 9: // 9XY0 - SKIP VX != VY
        {
            DEBUG_MSG("9XY0 | SKIP V[0x%01X] != V[0x%01X]\n", X, Y);

            if (memory->registers[X] != memory->registers[Y]) advance_pc(memory);
            break;
        }

    case 0xA: // ANNN - I = nnn
        {
            DEBUG_MSG("ANNN | I = 0x%03X\n", nnn);
            memory->i = nnn;
            break;
        }

    case 0xB: // BNNN - JUMP nnn + V0
        {
            DEBUG_MSG("ANNN | JUMP 0x%03X + V[0]\n", nnn);
            memory->pc = nnn + memory->registers[0];
            break;
        }

    case 0xC: // CXNN - Random NN
        {
            DEBUG_MSG("CXNN | V[0x%01X] = RAND & 0x%02X\n", X, kk);
            memory->registers[X] = (rand() % 0xFF) & kk;
            break;
        }

    case 0xD: // DXYN - Draw Vx Vy n
        {
            DEBUG_MSG("DXYN | DRAW V[0x%01X] V[0x%01X] 0x%01x\n", X, Y, N);

            int x = memory->registers[X] % CHIP8_DISPLAY_WIDTH;
            int y = memory->registers[Y] % CHIP8_DISPLAY_HEIGHT;
            memory->registers[0xF] = 0;
            for (int i = 0; i < N; i++)
            {
                int yPos = y + i;
                if (yPos >= CHIP8_DISPLAY_HEIGHT) break;

                uint8_t byte = memory->memory[memory->i + i];
                for (int xO = 7; xO >= 0; xO--)
                {
                    int xPos = x + 7 - xO;
                    if (xPos > CHIP8_DISPLAY_WIDTH) break;

                    uint8_t bit = (byte >> (xO)) & 0x1;
                    int index = yPos * CHIP8_DISPLAY_WIDTH + xPos;
                    uint8_t before = memory->display_memory[index];
                    memory->display_memory[index] ^= bit;

                    if (before != memory->display_memory[index]) memory->registers[0xF] = 0;
                }
            }
            break;
        }

    case 0xE:
        {
            switch (kk)
            {
            case 0x9E: // EX9E - SKIP KEY = Vx
                {
                    DEBUG_MSG("EX9E | SKIP KEY = V[0x%01X]\n", X);

                    char c = currentKeyPress();
                    if (c == 0x10) break;

                    if (memory->registers[X] == c) advance_pc(memory);
                    break;
                }
            case 0xA1: // EXA1 - SKIP KEY = Vx
                {
                    DEBUG_MSG("EXA1 | SKIP KEY != V[0x%01X]\n", X);

                    char c = currentKeyPress();
                    if (c == 0x10) break;

                    if (memory->registers[X] != c) advance_pc(memory);
                    break;
                }
            default:
                DEBUG_MSG("Invalid Instruction\n");
                exit(-1);
            }
            break;
        }

    case 0xF:
        {
            switch (kk)
            {
            case 0x07: // FX07 - Vx = DT
                DEBUG_MSG("FX07 | V[0x%01X] = DT\n", X);

                memory->registers[X] = memory->delay_timer;
                break;
            case 0x0A: // FX0A - Vx = KEY
                {
                    DEBUG_MSG("FX0A | V[0x%01X] = KEY\n", X);

                    char c = currentKeyPress();
                    if (c == 0x10)
                    {
                        memory->pc--;
                    }
                    else
                    {
                        memory->registers[X] = c;
                    }
                    break;
                }
            case 0x15: // FX15 - DT = Vx
                DEBUG_MSG("FX15 | DT = V[0x%01X]\n", X);

                memory->delay_timer = memory->registers[X];
                break;
            case 0x18: // FX18 - ST = Vx
                DEBUG_MSG("FX0A | ST = V[0x%01X]\n", X);

                memory->sound_timer = memory->registers[X];
                break;
            case 0x1E: // FX1E - I += Vx
                DEBUG_MSG("FX1E | I += V[0x%01X]\n", X);

                memory->i = memory->i + memory->registers[X];
                break;
            case 0x29: // FX29 - I = MEM DIGIT Vx
                DEBUG_MSG("FX29 | I = MEM DIGIT V[0x%01X]\n", X);

                memory->i = nibbleToSprite(memory->registers[X]);
                break;
            case 0x33: // FX33 - I, I+1, I+2 = BCD Vx
                DEBUG_MSG("FX33 | I, I+1, I+2 = V[0x%01X]\n", X);

                memory->memory[memory->i] = memory->registers[X] / 100;
                memory->memory[memory->i + 1] = (memory->registers[X] / 10) % 10;
                memory->memory[memory->i + 2] = (memory->registers[X] / 1) % 10;
                break;
            case 0x55: // FX55 - STR Vx
                DEBUG_MSG("FX55 | STR V[0x%01X]\n", X);

                for (int i = 0; i <= X; i++)
                {
                    memory->memory[memory->i + i] = memory->registers[i];
                }
                break;
            case 0x65: // FX65 - LD Vx
                DEBUG_MSG("FX65 | LD V[0x%01X]\n", X);

                for (int i = 0; i <= X; i++)
                {
                    memory->registers[i] = memory->memory[memory->i + i];
                }
                break;
            default:
                DEBUG_MSG("Invalid Instruction\n");
                exit(-1);
                break;
            }
            break;
        }
    default:
        DEBUG_MSG("Invalid Instruction\n");
        exit(-1);
        break;
    }
}

void printMemory(struct chip8_memory* memory, int from, int to)
{
    int lineLimit = 16;

    int upperBound = (to / lineLimit) * 16;

    printf("      | ");
    for (int i = 0; i < lineLimit; i++)
    {
        printf("%02X ", i);
    }
    printf("\n");
    printf("------|------------------------------------------------\n");

    for (int i = 0;; i++)
    {
        int line = i * lineLimit + from;
        if (line > upperBound) break;

        printf("0x%03X | ", line);
        for (int j = 0; j < lineLimit; j++)
        {
            printf("%02X ", memory->memory[line + j]);
        }
        printf("\n");
    }
}

void printDisplayMemory(struct chip8_memory* memory)
{
    int lineLimit = 64;
    int rows = 32;

    for (int i = 0; i < rows; i++)
    {
        int line = i * lineLimit;

        for (int j = 0; j < lineLimit; j++)
        {
            int c = memory->display_memory[line + j];
            printf("%01X%01X", c, c);
        }
        printf("\n");
    }
}

void render_display(struct chip8_memory* memory)
{
#ifdef DISPLAY
    const uint8_t yOffset = 1;
    for (int y = 0; y < CHIP8_DISPLAY_HEIGHT; y++)
    {
        for (int x = 0; x < CHIP8_DISPLAY_WIDTH; x++)
        {
            int renderY = y + yOffset;
            move(renderY, x * 2 + 1);
            delch();
            move(renderY, x * 2);
            delch();

            int index = y * CHIP8_DISPLAY_WIDTH + x;
            if (memory->display_memory[index])
            {
                mvprintw(renderY, x * 2, RENDER_CHARACTER);
            }
            else
            {
                mvprintw(renderY, x * 2, "  ");
            }
        }
    }
#endif
}

void update(chip8_memory_T memory)
{
    if (memory->sound_timer)
    {
        fprintf(stdin, "%c", '\007');
    }

    memory->sound_timer = (memory->sound_timer != 0) ? memory->sound_timer - 1 : 0;
    memory->delay_timer = (memory->delay_timer != 0) ? memory->delay_timer - 1 : 0;

    render_display(memory);
}

void mainLoop(uint8_t* bytes, int byteCount)
{
    timer_T game_timer;
    timer_T display_timer;

    gettimeofday(&game_timer.previous, NULL);
    gettimeofday(&display_timer.previous, NULL);

    const float game_clk = 1000000. / 1024.;
    const float display_clk = 1000000. / 60.;

    chip8_memory_T memory = init_memory();

    memcpy(memory->memory + 0x200, bytes, byteCount);

    do
    {
        float instruction_time = get_timer_delta(&game_timer);
        float display_time = get_timer_delta(&display_timer);

        uint8_t update_game = update_timer(&game_timer, game_clk);
        uint8_t update_display = update_timer(&display_timer, display_clk);

        if (!update_game)
        {
            continue;
        }

        if (update_display)
        {
            update(memory);
            mvprintw(0, 0,
                     "Instruction Time: %.6f | IPS: %.2f | Frame Time: %.6f | FPS: "
                     "%.2f | %02X      ",
                     instruction_time, 1. / instruction_time, display_time, 1. / display_time,
                     currentKeyPress());

            refresh();
        }

        uint16_t next_instruction = get_next_instruction(memory);
        execute(next_instruction, memory);

    } while (1);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        printf("Invalid Usage: Chip8 <file_location>");
        return -1;
    }

    char* rom_location = argv[1];
    int byte_count;
    uint8_t* bytes = read_rom(rom_location, &byte_count);

#ifdef DISPLAY
    initscr();
    cbreak();
    nodelay(stdscr, 1);
    refresh();
#endif

    mainLoop(bytes, byte_count);

    free(bytes);

#ifdef DISPLAY
    endwin();
#endif

    return 0;
}
