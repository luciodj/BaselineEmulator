/*
** PIC16 BASE LINE (32-bit) emulator
**
** LDJ 7/22/09 First draft
*/

//#define DBG
#define TIMED

#include <plib.h>
#include <inttypes.h>

// configuration bit settings, Fcy=80MHz, Fpb=40MHz
#pragma config POSCMOD=XT, FNOSC=PRIPLL 
#pragma config FPLLIDIV=DIV_2, FPLLMUL=MUL_20, FPLLODIV=DIV_1
#pragma config FPBDIV=DIV_1, FWDTEN=OFF, CP=OFF, BWP=OFF
#define FPB     80000000
#define FSY     80000000

//#define uint8_t       unsigned char   // 8-bit base type
//#define uint16      unsigned short  // 16-bit base type

#define ROMSIZE     512     // program memory size in (12-bit) words (sun of all banks)
#define RAMSIZE     32      // data memory size in bytes (sum of all banks)
#define STACKDEPTH  2       // depth of hardware stack

// FILE registers naming convention
#define rINDF   00          // indirect file pointer (*rFSR)
#define rTMR0   01
#define rPCL    02          // program counter Low 
#define rSTATUS 03          // the CPU status register
#define rFSR    04          // the file selection register
#define rPORTA  05          // fist I/O port
#define rPORTB  06          // second I/O port
#define MAXSFR      7       // number of SFRs

// instruction format        
typedef union
{
    struct
    {
        unsigned iF:    5;  // file register operand
        unsigned iB:    3;      // bit operand
    };

    struct
    {
        unsigned iK:    8;      // 8-bit literal 
        unsigned iOp:   4;      // opcode
    };
    
    uint16_t opcode;
} dcode;

dcode ir;

// STATUS flags
unsigned C;
unsigned DC;
unsigned Z; 
unsigned nPD;
unsigned nTO;
unsigned PA;

// Byte oriented instructions
#define MOVWF   0x00    // special
#define NOP     0x00    // special 
#define CLRF    0x01    // special
#define CLRW    0x01    // special
#define SUBWF   0x02    
#define DECF    0x03    

// 01
#define IORWF   0x00
#define ANDWF   0x01
#define XORWF   0x02
#define ADDWF   0x03

// 02
#define MOVF    0x00
#define COMF    0x01
#define INCF    0x02
#define DECFSZ  0x03

// 03
#define RRF     0x00
#define RLF     0x01
#define SWAPF   0x02
#define INCFSZ  0x03

// bit oriented instructions
#define BCF     0x04
#define BSF     0x05
#define BTFSC   0x06
#define BTFSS   0x07

// literal and control instructions
#define CLRWDT  0x00    // special
#define SLEEP   0x00    // special
#define TRIS    0x00    // special
#define OPTION  0x00    // special 

#define RETLW   0x08
#define CALL    0x09
#define GOTO0   0x0A
#define GOTO1   0x0B
#define MOVLW   0x0C
#define IORLW   0x0D
#define ANDLW   0x0E
#define XORLW   0x0F


// program memory
//uint16 rom[ ROMSIZE];
#include "test1.rom"

// registers file
uint8_t ram[ RAMSIZE];

// hardware stack
uint16_t stack[ STACKDEPTH];

// machine state
uint16_t pPC;
uint8_t  pW, pSP, pWDT, pPS, fSleep;
uint16_t pTRISA, pTRISB, pOPTION;

// debugging options
#ifdef DBG
char s[128];         // disassembly string
int bkpt = 0xffff;
#define DIS(x) strcat(s, x)

#else
#define DIS(x) 
#endif

// hw stack emulation        
void push( uint16_t pc)
{
    if ( pSP< STACKDEPTH)
        stack[ pSP++] = pc;
    else 
        return;             // overflow 
} // push    

uint16_t pop( void)
{
    if ( pSP > 0) 
        return stack[--pSP];
    else 
        return 0;       // underflow
} // pop    


int fTRIS( int i, int v)
{
    switch( i & 3)
    {
    case 0: 
        pTRISA = v&0xf;
        TRISA = pTRISA;
        break;
    case 1:
        pTRISB = v;
        TRISB = pTRISB;
        break;
    default:
        break;// unimplemented
    }// switch i    
    
} // fTRIS 
  
uint8_t readFILE( uint8_t x)    
{ 
     int r; 
    if ((x) < MAXSFR) 
    { 
        switch( (x))
        { 
        case rPORTA: // PORTA  
            return PORTA; 
            break; 
        case rPORTB: // PORTB 
            return PORTB; 
            break; 
        case rINDF:  //INDF
            r = ram[ram[rFSR]]; 
            break;  
        case rTMR0: // TMR0 
            r = ReadTimer1(); 
            break; 
        case rPCL: // PCL
            r = pPC; 
            break;
        case rSTATUS: // STATUS 
            r = C + (DC<<1) + (Z<<2) + (nPD<<3) + (nTO<<4) +  (PA<<5); 
            break; 
        case rFSR: // FSR 
            r = ram[rFSR]; 
            break; 
        } 
        return r; 
    } 
     
    else 
        return ram[ (x)];         
} //readFILE 


 void writeFILE( int x, uint8_t v)
{
    if (x < MAXSFR)
    {
        switch( x)
        {
        case rPORTA: // PORTA
            LATA = v;
            break;
        case rPORTB: // PORTB
            LATB = v;
            break;
        case rINDF: //INDF
           ram[ram[rFSR]] = v;
            break;
        case rTMR0: // TMR0
            WriteTimer1( v);
            break;
        case rPCL: // PCL
            pPC = v;
            break;
        case rSTATUS: // STATUS
            C = v & 1;
            DC = (v>>1) & 1;
            Z= (v>>2) & 1;
            PA = (v>>5) & 3;
            break;
        case rFSR: // FSR
            ram[rFSR] = v;
            break;
        }
    } //if 
    
    else
        ram[ x] = v;   
}// writeFILE        
    
// instruction decoding and execution
int Decode( dcode ir)
{   // decode and return PC increment ( 1- sequential  or 2-skip)
    int skip = 1;
    int r, t;
    
    switch( ir.iOp) 
    {
    case 00:    // byte oriented operations
        switch( ir.iB>>1)
        {
        case 00:   // entire special group
            if (ir.iB & 1)   // MOVWF
            {  
                DIS("MOVWF");
                writeFILE( ir.iF,  pW);
            }
            
        // MOVWF TRIS SLEEP OPTION CLRWDT
            else switch( ir.iK)
            {
            case 00:    // NOP
                DIS( "NOP");
                break;
            case 02:    // OPTION
                DIS( "OPTION");
                pOPTION = pW;
                break;
            case 03:    // SLEEP
                DIS( "SLEEP");
                nTO = 1;
                nPD = 0;
                fSleep = 1; 
                break;
            case 04:    // CLRWDT
                DIS( "CLRWDT");
                nTO = 1;
                nPD = 0;
                pWDT = 0;
                break;
            case 05:    // TRISA
                DIS( "TRISA");
                fTRIS( ir.iK -5, pW);
                break;
            case 06:    // TRISB
                DIS( "TRISB");
                fTRIS( ir.iK -5, pW);
                break;
            default:
                DIS( "UNKNOWN");
                while(1);   // unimplemented
                break;
            } // switch iK
            break;
        case 01:      // CLR instructions   
            DIS( "CLR");
            if (ir.iB&1)  // CLRF
                writeFILE( ir.iF,  0);
            else        // CLRW
                pW = 0;
            Z = 1;     // set Z flag
            break;
 
        case SUBWF:   // 0x02:    
            DIS("SUBWF");
            r = (readFILE(ir.iF) - pW);
            Z = (r == 0);
            C != (r < 0);
            DC = ((r & 0xf) > 9);
            // write back if destination is file or place in W
            if (ir.iB&1) 
                writeFILE( ir.iF,  r);
            else 
                pW = r;
            break;
        case DECF:    // 0x03:   
            DIS("DECF");
            r = (readFILE(ir.iF) -1);
            Z = (r == 0);
            // write back if destination is file or place in W
            if (ir.iB&1) 
                writeFILE( ir.iF,  r);
            else 
                pW = r;
                break;
        } // switch ir.iOp2
        break;
        
    case 01:// byte oriented operations
        switch( ir.iB>>1)
        {
        case IORWF:   //  0x04:
            DIS("IORWF");
            r = (readFILE(ir.iF) | pW);
            Z = (r == 0);
            break;
        case ANDWF:   // 0x05:
            DIS("ANDWF");
            r = (readFILE( ir.iF) & pW);
            Z = (r == 0);
            break;
        case XORWF:   // 0x06:
            DIS("XORWF");
            r = (readFILE( ir.iF) ^ pW);
            Z = (r == 0);
            break;
        case ADDWF:   // 0x07:
            DIS("ADDWF");
            r = (readFILE( ir.iF) + pW);
            Z = (r == 0);
            C = (r <= 255);
            DC = ((r & 0xf) > 9);
            break;
        } // switch    
        // write back if destination is file or place in W
        if (ir.iB&1) 
            writeFILE( ir.iF,  r);
        else 
            pW = r;
        break;
    
    case 02:// byte oriented operations
        switch( ir.iB>>1)
        {
        case MOVF:    // 0x08:
            DIS("MOVF");
            r = (readFILE( ir.iF));
            Z = (r == 0);
            break;
        case COMF:    // 0x09:
            DIS("COMF");
            r = ~(readFILE( ir.iF));
            Z = (r == 0);
            break;
        case INCF:    // 0x0A:
            DIS("INCF");
            r = (readFILE( ir.iF)+1);
            Z = (r == 0);
            break;
        case DECFSZ:  // 0x0B:
            DIS("DECFSZ");
            r = (readFILE( ir.iF)-1);
            skip += (r == 0);
            break;
        }// switch
        // write back if destination is file or place in W
        if (ir.iB&1) 
            writeFILE( ir.iF,  r);
        else 
            pW = r;
        break;
            
    case 03:    // byte oriented operations
        switch( ir.iB>>1)
        {
        case RRF:     // 0x0C:
            DIS("RRF");
            t = readFILE( ir.iF);
            r = (C<<8) | (t>>1);
            C = t & 1;
            break;
        case RLF:     // 0x0D:
            DIS("RLF");
            r = (C) + (readFILE( ir.iF)<<1);
            C = (readFILE( ir.iF)>>8);
            break;
        case SWAPF:   // 0x0E:
            DIS("SWAPF");
            r = (readFILE( ir.iF)<<4) | (readFILE( ir.iF)>>4);
            break;
        case INCFSZ:  // 0x0F:
            DIS("INCFSZ");
            r = (readFILE( ir.iF)+1);
            skip += (r == 0);
            break;
        }
        // write back if destination is file or place in W
        if (ir.iB&1) 
            writeFILE( ir.iF,  r);
        else 
            pW = r;
        break;
    
    case BCF:
        DIS("BCF");
        writeFILE(ir.iF, readFILE(ir.iF) & ~(1<<ir.iB));
        break;
    case BSF:
        DIS("BSF");
        writeFILE(ir.iF, readFILE(ir.iF) | (1<<ir.iB));
        break;
    case BTFSC:
        DIS("BTFSC");
        skip += ((readFILE( ir.iF) & (1<<ir.iB)) == 0);
        break;
    case BTFSS:
        DIS("BTFSS");
        skip += ((readFILE( ir.iF) & (1<<ir.iB)) != 0);
        break;
        
    // literal operations    
    case RETLW:
        DIS("RETLW");
        pW = ir.iK; 
        pPC = pop();
#ifdef TIMED        
        // wait for one clock cycle completion
        while( !mT1GetIntFlag());
        mT1ClearIntFlag();
#endif        
        break;
        
    case CALL:
        DIS("CALL");
        push(pPC); pPC = ir.iK;
#ifdef TIMED        
        // wait for one clock cycle completion
        while( !mT1GetIntFlag());
        mT1ClearIntFlag();
#endif        
        break;
        
    case GOTO0:
        DIS("GOTO");
        pPC = ir.iK -1;
#ifdef TIMED        
        // wait for one clock cycle completion
        while( !mT1GetIntFlag());
        mT1ClearIntFlag();
#endif        
        break;
        
    case GOTO1:
        DIS("GOTO");
        pPC = 256 + ir.iK -1;
#ifdef TIMED        
        // wait for one clock cycle completion
        _RA0 = 1;
        while( !mT1GetIntFlag());
        mT1ClearIntFlag();
        _RA0 = 0;
#endif        
        break;
    case MOVLW:
        DIS("MOVLW");
        pW = ir.iK;
        break;
    case IORLW:
        DIS("IORLW");
        pW |= ir.iK;
        break;
    case ANDLW:
        DIS("ANDLW");
        pW &= ir.iK;
        break;
    case XORLW:
        DIS("XORLW");
        pW ^= ir.iK;
        break;
    default:
        break;      // error invalid opcode
    } // switch
    
    return skip;
} // decode    
    
int main( void)
{
    SYSTEMConfig( FSY, SYS_CFG_PCACHE | SYS_CFG_WAIT_STATES);
    
#ifdef DBG    
    OpenUART1(UART_EN, UART_TX_ENABLE, 1);
#endif

// machine status initialization
    pPC = 0;        // reset vector
    pTRISA = 0x0f;
    pTRISB = 0xff;
    pOPTION = 0x3f;
    pWDT = 0;
    pPS = 0;
    fSleep = 0;
    ram[rSTATUS] = 0x18;    
    
    // main timer initialization
    OpenTimer1( T1_ON, FPB/1000000-1);    // 1us tick equiv. PIC16 @4MHz clock
    mT1ClearIntFlag();
    
    // main simulation loop
    while(1)
    {
#ifdef DBG
    // s = address before decoding
    sprintf( s, "%04X ", pPC);

    if ( pPC==bkpt)
        while(1);       
#endif
        
        // decode new instruction
        pPC += Decode( (dcode)rom[pPC]);
#ifdef TIMED        
        // wait for one clock cycle completion
        _RA0 = 1;
        while( !mT1GetIntFlag());
        mT1ClearIntFlag();
        _RA0 = 0;
#endif        
                 
#ifdef DBG
    // show executed instruction disassembly and status
    putsUART1(s);
    sprintf(s, "    C=%d, DC=%d, Z=%d, W=%02X, PORTA=%02X, TRISA=%02X\n", 
                C, DC, Z, pW, ram[rPORTA], pTRISA);
    putsUART1(s);

    // make a copy of registers before instruction execution
    //for(i=0; i<12; i++)
    //    oreg[i]=reg[i];
    //oflags.b=flags.b;                            
#endif                
    }// main loop
}    
