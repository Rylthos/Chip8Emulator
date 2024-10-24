#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/select.h>
#include <sys/time.h>

#include <curses.h>

#include "sprites.h"

// #define DEBUG
#define DISPLAY

#ifdef DEBUG
#define DEBUG_MSG(...) printf(__VA_ARGS__)
#else
#define DEBUG_MSG(msg, ...)
#endif

#define CHIP8_DISPLAY_WIDTH 64
#define CHIP8_DISPLAY_HEIGHT 32

#define RENDER_CHARACTER "@@"

struct chip8_memory {
    uint16_t pc;
    uint16_t sp;
    int16_t i;

    uint8_t registers[16];
    uint16_t stack[16];
    uint8_t memory[4096];

    uint8_t display_memory[CHIP8_DISPLAY_WIDTH * CHIP8_DISPLAY_HEIGHT];

    uint8_t delay_timer;
    uint8_t sound_timer;
};

FILE* read_rom(char* file)
{
    FILE* ptr;
    ptr = fopen(file, "rb");
    return ptr;
}

int nibbleToSprite(uint8_t v) { return 5 * (v & 0xF); }

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

    DEBUG_MSG("0x%04X, PC: 0x%04X\n", instruction, memory->pc);

    switch (opcode)
    {
    case 0:
        {
            if (nnn == 0x0E0) // 00E0 - Display
            {
                memset(memory->display_memory, 0, 64 * 32);
                break;
            }
            else if (nnn == 0x0EE) // 00EE -- RETURN
            {
                memory->pc = memory->stack[memory->sp];
                memory->sp--;
                break;
            }

            DEBUG_MSG("Invalid operation : %X\n", instruction);
            exit(-1);
            break;
        }

    case 1: // 1NNN - Goto NNN
        {
            DEBUG_MSG("Jump to 0x%X\n", nnn);
            memory->pc = nnn;
            break;
        }

    case 2: // 2NNN - Call NNN
        {
            DEBUG_MSG("Call 0x%X\n", nnn);
            memory->sp++;
            memory->stack[memory->sp] = memory->pc;
            memory->pc = nnn;
            break;
        }

    case 3: // 3XNN - if (Vx == NN)
        {
            if (memory->registers[X] == kk) memory->pc += 2;
            break;
        }

    case 4: // 4XNN - if (Vx != NN)
        {
            if (memory->registers[X] != kk) memory->pc += 2;
            break;
        }

    case 5: // 5XY0 - if (Vx == Vy)
        {
            if (memory->registers[X] == memory->registers[Y]) memory->pc += 2;
            break;
        }

    case 6: // 6XNN - Vx = NN
        {
            memory->registers[X] = kk;
            break;
        }
    case 7: // 7xNN - Vx += NN
        {
            memory->registers[X] += kk;
            break;
        }

    case 8:
        {
            switch (N)
            {
            case 0: // 8XY0 - Vx = Vy
                memory->registers[X] = memory->registers[Y];
                break;
            case 1: // 8XY1 - Vx |= Vy
                memory->registers[X] |= memory->registers[Y];
                memory->registers[0xF] = 0;
                break;
            case 2: // 8XY2 - Vx &= V1100y
                memory->registers[X] &= memory->registers[Y];
                memory->registers[0xF] = 0;
                break;
            case 3: // 8XY3 - Vx ^= Vy
                memory->registers[X] ^= memory->registers[Y];
                memory->registers[0xF] = 0;
                break;
            case 4: // 8XY4 - Vx += Vy
                {
                    int16_t before = memory->registers[X];
                    before += memory->registers[Y];
                    memory->registers[X] = before & 0xFF;
                    memory->registers[0xF] = (before > 0xFF);
                    break;
                }
            case 5: // 8XY5 - Vx -= Vy
                {
                    uint8_t rv = (memory->registers[X] >= memory->registers[Y]);
                    memory->registers[X] -= memory->registers[Y];
                    memory->registers[0xF] = rv;
                    break;
                }
            case 6: // 8XY6 - Vx >>= 1
                {
                    uint8_t rv = (memory->registers[X] & 0x1);
                    memory->registers[X] >>= 1;
                    memory->registers[0xF] = rv;
                    break;
                }
            case 7: // 8XY7 - Vx = Vy - Vx
                {
                    uint8_t rv = (memory->registers[Y] >= memory->registers[X]);
                    memory->registers[X] = memory->registers[Y] - memory->registers[X];
                    memory->registers[0xF] = rv;
                    break;
                }

            case 0xE: // 8XYE - Vx = vX << 1
                {
                    uint8_t rv = (memory->registers[X] >> 7) & 1;
                    memory->registers[X] <<= 1;
                    memory->registers[0xF] = rv;
                    break;
                }
            default:
                printf("Invalid operation: %x\n", instruction);
            }
            break;
        }

    case 9:
        {
            if (memory->registers[X] != memory->registers[Y]) memory->pc += 2;
            break;
        }

    case 0xA:
        memory->i = nnn;
        break;

    case 0xB:
        memory->pc = nnn + memory->registers[0];
        break;

    case 0xC:
        memory->registers[X] = (rand() % 0xFF) & kk;
        break;

    case 0xD:
        {
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
            case 0x9E:
                {
                    char c = currentKeyPress();
                    if (c == 0x10) break;

                    if (memory->registers[X] == c) memory->pc += 2;
                    break;
                }
            case 0xA1:
                {
                    char c = currentKeyPress();
                    if (c == 0x10) break;

                    if (memory->registers[X] != c) memory->pc += 2;
                    break;
                }
            default:
                printf("Invalid instruction: %x\n", instruction);
            }
            break;
        }

    case 0xF:
        {
            switch (kk)
            {
            case 0x07:
                memory->registers[X] = memory->delay_timer;
                break;
            case 0x0A:
                {
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
            case 0x15:
                memory->delay_timer = memory->registers[X];
                break;
            case 0x18:
                memory->sound_timer = memory->registers[X];
                break;
            case 0x1E:
                memory->i = memory->i + memory->registers[X];
                break;
            case 0x29:
                memory->i = nibbleToSprite(memory->registers[X]);
                break;
            case 0x33:
                memory->memory[memory->i] = memory->registers[X] / 100;
                memory->memory[memory->i + 1] = (memory->registers[X] / 10) % 10;
                memory->memory[memory->i + 2] = (memory->registers[X] / 1) % 10;
                break;
            case 0x55:
                for (int i = 0; i <= X; i++)
                {
                    memory->memory[memory->i + i] = memory->registers[i];
                }
                break;
            case 0x65:
                for (int i = 0; i <= X; i++)
                {
                    memory->registers[i] = memory->memory[memory->i + i];
                }
                break;
            default:
                printf("Invalid opcode: %x\n", instruction);
                break;
            }
            break;
        }
    default:
        printf("Invalid opcode: %x\n", instruction);
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

void renderDisplay(struct chip8_memory* memory)
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
    refresh();
#endif
}

void mainLoop(uint8_t* bytes, int byteCount)
{
    struct timeval current;
    struct timeval previous;

    struct timeval timer_current;
    struct timeval timer_previous;

    gettimeofday(&previous, NULL);
    gettimeofday(&timer_previous, NULL);

    float wait = 1000000. / 1024.;
    float timer_wait = 1000000. / 60.;

    struct chip8_memory memory;
    memory.pc = 0x200;
    memory.sp = 0;

    for (int i = 0; i < 16; i++)
    {
        int location = nibbleToSprite(i);
        for (int j = 0; j < 5; j++)
        {
            memory.memory[location + j] = fonts[i][j];
        }
    }

    memset(memory.memory, 0, 4096);
    memcpy(memory.memory + 0x200, bytes, byteCount);

    memset(memory.stack, 0, 16);
    memset(memory.display_memory, 0, CHIP8_DISPLAY_WIDTH * CHIP8_DISPLAY_HEIGHT);

    memory.memory[0x1ff] = 3;

    do
    {
        gettimeofday(&current, NULL);
        gettimeofday(&timer_current, NULL);

        if (current.tv_usec < previous.tv_usec)
        {
            previous = current;
        }

        if (timer_current.tv_usec < timer_previous.tv_usec)
        {
            timer_previous = timer_current;
        }

        if (current.tv_usec - previous.tv_usec < wait)
        {
            continue;
        }
        int64_t frame_time = current.tv_usec - previous.tv_usec;
        previous = current;

        if (timer_current.tv_usec - timer_previous.tv_usec > timer_wait)
        {
            float diff = timer_current.tv_usec - timer_previous.tv_usec;
            float diff_seconds = diff / 1000000.0;

            if (memory.sound_timer)
            {
                fprintf(stdin, "%c", '\007');
            }

            timer_previous = timer_current;
            memory.sound_timer = (memory.sound_timer != 0) ? memory.sound_timer - 1 : 0;
            memory.delay_timer = (memory.delay_timer != 0) ? memory.delay_timer - 1 : 0;

            mvprintw(0, 0,
                     "Instruction Time: %.6f | IPS: %.2f | Frame Time: %.6f | FPS: "
                     "%.2f | %02X      ",
                     frame_time / 1000000.0, 1000000.0 / frame_time, diff_seconds, 1 / diff_seconds,
                     currentKeyPress());
            renderDisplay(&memory);
        }

        uint16_t currentInstruction = 0;
        currentInstruction |= (memory.memory[memory.pc] << 8);
        currentInstruction |= (memory.memory[memory.pc + 1]);
        // uint16_t currentInstruction = memory.memory[memory.pc];

        memory.pc += 2;
        execute(currentInstruction, &memory);

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
    FILE* rom = read_rom(rom_location);

    fseek(rom, 0, SEEK_END);
    uint32_t romSize = ftell(rom);
    fseek(rom, 0, SEEK_SET);

    printf("Rom Size: %u\n", romSize);

    uint8_t* bytes = (uint8_t*)malloc(sizeof(uint8_t) * romSize);

    int byteCount = fread(bytes, sizeof(uint8_t), romSize, rom);
    fclose(rom);

#ifdef DISPLAY
    initscr();
    cbreak();
    nodelay(stdscr, 1);
    refresh();
#endif
    mainLoop(bytes, byteCount);

    free(bytes);

#ifdef DISPLAY
    endwin();
#endif

    return 0;
}
