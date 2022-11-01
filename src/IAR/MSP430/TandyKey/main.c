// Include the device header for the IAR processor setting
#include <msp430.h>
#include <stdint.h>

// Set to 0 to disable watchdog timer, set to 1 to enable.
#define USE_WDT  1

// IO bit assignments
// P1
// Tandy keyboard (output)
#define KBD_BSYN BIT0  // input
#define KBD_CLK  BIT3  // output
#define KBD_DTA  BIT4  // output

// PS2 keyboard (input)
#define PS2_DTA  BIT6  // input
#define PS2_CLK  BIT7  // input

// UART (input/output)
#define UART_RX  BIT1  // input (also BSL_TX)
#define UART_TX  BIT2  // output

// BSL
// used by bootstrap loader
#define BSL_TX   BIT1
#define BSL_RX   BIT5

// P2
// LED's
#define LED1     BIT0  // output
#define LED0     BIT7  // output

// Output bit rate
// The PS2 clock is between 10 kHz and 16.7 kHz.  The baud specified here is baud of
// the output keyboard clock.  This doesn't have to be a standard baud nor does it
// have to conform to the PS2 clock requirement, but the characters are also output
// in UART format so a standard baud within the PS2 range is used.  For the Tandy
// serial format it is convenient to use a 4x clock so 14.4k might be approaching
// the practical maximum for an 8MHz cpu clock.  The cpu can run at 16MHz though so
// this could be increased if the cpu clock is increased.
#define BAUD     14400L

// Timer period for 4*baud clock
#define PERIOD0  (8000000L/(BAUD*4L))  // 8MHz cpu

volatile uint8_t ps2_hld;
volatile uint8_t ps2_hld_rdy = 0;

#define KBD_BUF_N  16  // must be power of 2
volatile uint8_t kbd_buf[KBD_BUF_N];
uint8_t kbd_buf_in;
volatile uint8_t kbd_buf_out;

const struct
   {
   uint16_t code;
   uint8_t  ctrl;
   uint8_t  shft;
   uint8_t  alt;
   uint8_t  none;
   } ps2_map[] =
   {
      // [0..25]
      // for A-Z the CapsLock swaps the shift and none columns
      // code, ctrl, shft,  alt, none,
      { 0x01c, 0x01, 0x41, 0xc1, 0x61, }, // A
      { 0x032, 0x02, 0x42, 0xc2, 0x62, }, // B
      { 0x021, 0x03, 0x43, 0xc3, 0x63, }, // C
      { 0x023, 0x04, 0x44, 0xc4, 0x64, }, // D
      { 0x024, 0x05, 0x45, 0xc5, 0x65, }, // E
      { 0x02b, 0x06, 0x46, 0xc6, 0x66, }, // F
      { 0x034, 0x07, 0x47, 0xc7, 0x67, }, // G
      { 0x033, 0x08, 0x48, 0xc8, 0x68, }, // H
      { 0x043, 0x09, 0x49, 0xc9, 0x69, }, // I
      { 0x03b, 0x0a, 0x4a, 0xca, 0x6a, }, // J
      { 0x042, 0x0b, 0x4b, 0xcb, 0x6b, }, // K
      { 0x04b, 0x0c, 0x4c, 0xcc, 0x6c, }, // L
      { 0x03a, 0x0d, 0x4d, 0xcd, 0x6d, }, // M
      { 0x031, 0x0e, 0x4e, 0xce, 0x6e, }, // N
      { 0x044, 0x0f, 0x4f, 0xcf, 0x6f, }, // O
      { 0x04d, 0x10, 0x50, 0xd0, 0x70, }, // P
      { 0x015, 0x11, 0x51, 0xd1, 0x71, }, // Q
      { 0x02d, 0x12, 0x52, 0xd2, 0x72, }, // R
      { 0x01b, 0x13, 0x53, 0xd3, 0x73, }, // S
      { 0x02c, 0x14, 0x54, 0xd4, 0x74, }, // T
      { 0x03c, 0x15, 0x55, 0xd5, 0x75, }, // U
      { 0x02a, 0x16, 0x56, 0xd6, 0x76, }, // V
      { 0x01d, 0x17, 0x57, 0xd7, 0x77, }, // W
      { 0x022, 0x18, 0x58, 0xd8, 0x78, }, // X
      { 0x035, 0x19, 0x59, 0xd9, 0x79, }, // Y
      { 0x01a, 0x1a, 0x5a, 0xda, 0x7a, }, // Z

      // [26..30]
      // code, ctrl, shft,  alt, none,
      { 0x05a, 0x0d, 0x0d, 0x0d, 0x0d, }, // ENTER
      { 0x029, 0x20, 0x20, 0x20, 0x20, }, // SPACE
      { 0x066, 0x08, 0x08, 0x08, 0x08, }, // BACKSPACE
      { 0x00d, 0x09, 0x09, 0x09, 0x09, }, // TAB
      { 0x076, 0x1b, 0x1b, 0x1b, 0x1b, }, // ESC

      // [31..40]
      { 0x045, 0x7c, 0x29, 0xb0, 0x30, }, // 0 )
      { 0x016, 0xa1, 0x21, 0xb1, 0x31, }, // 1 !
      { 0x01e, 0xc0, 0x40, 0xb2, 0x32, }, // 2 @
      { 0x026, 0xa3, 0x23, 0xb3, 0x33, }, // 3 #
      { 0x025, 0xa4, 0x24, 0xb4, 0x34, }, // 4 $
      { 0x02e, 0xa5, 0x25, 0xb5, 0x35, }, // 5 %
      { 0x036, 0x7e, 0x5e, 0xb6, 0x36, }, // 6 ^
      { 0x03d, 0xa6, 0x26, 0xb7, 0x37, }, // 7 &
      { 0x03e, 0xaa, 0x2a, 0xb8, 0x38, }, // 8 *
      { 0x046, 0x5c, 0x28, 0xb9, 0x39, }, // 9 (

      // [41..47]
      // code, ctrl, shft,  alt, none,
      { 0x052, 0xa2, 0x22, 0xa7, 0x27, }, // ' "
      { 0x041, 0xbc, 0x3c, 0xac, 0x2c, }, // , <
      { 0x04e, 0x7f, 0x5f, 0xad, 0x2d, }, // - _
      { 0x049, 0xbe, 0x3e, 0xae, 0x2e, }, // . >
      { 0x04a, 0xbf, 0x3f, 0xaf, 0x2f, }, // / ?
      { 0x04c, 0xba, 0x3a, 0xbb, 0x3b, }, // ; :
      { 0x055, 0xab, 0x2b, 0xbd, 0x3d, }, // = +

      // [48..50]
      // code, ctrl, shft,  alt, none,
      { 0x054, 0xdb, 0x7b, 0xfb, 0x5b, }, // [ {
      { 0x05d, 0xdc, 0x7c, 0xfc, 0x5c, }, // \ |
      { 0x05b, 0xdd, 0x7d, 0xfd, 0x5d, }, // ] }

      // [51..66]
      // code, ctrl, shft,  alt, none,
      { 0x070, 0x30, 0x30, 0x30, 0x30, }, // kp 0
      { 0x069, 0x31, 0x31, 0x31, 0x31, }, // kp 1
      { 0x072, 0x32, 0x32, 0x32, 0x32, }, // kp 2
      { 0x07a, 0x33, 0x33, 0x33, 0x33, }, // kp 3
      { 0x06b, 0x34, 0x34, 0x34, 0x34, }, // kp 4
      { 0x073, 0x35, 0x35, 0x35, 0x35, }, // kp 5
      { 0x074, 0x36, 0x36, 0x36, 0x36, }, // kp 6
      { 0x06c, 0x37, 0x37, 0x37, 0x37, }, // kp 7
      { 0x075, 0x38, 0x38, 0x38, 0x38, }, // kp 8
      { 0x07d, 0x39, 0x39, 0x39, 0x39, }, // kp 9
      { 0x07c, 0x2a, 0x2a, 0x2a, 0x2a, }, // kp *
      { 0x079, 0x2b, 0x2b, 0x2b, 0x2b, }, // kp +
      { 0x07b, 0x2d, 0x2d, 0x2d, 0x2d, }, // kp -
      { 0x071, 0x2e, 0x2e, 0x2e, 0x2e, }, // kp .
      { 0x14a, 0x2f, 0x2f, 0x2f, 0x2f, }, // kp /
      { 0x15a, 0x0d, 0x0d, 0x0d, 0x0d, }, // kp ENTER

      // [67..70]
      // code, ctrl, shft,  alt, none,
      { 0x16b, 0x1c, 0x1c, 0x1c, 0x1c, }, // LEFT ARROW
      { 0x174, 0x1d, 0x1d, 0x1d, 0x1d, }, // RIGHT ARROW
      { 0x175, 0x1e, 0x1e, 0x1e, 0x1e, }, // UP ARROW
      { 0x172, 0x1f, 0x1f, 0x1f, 0x1f, }, // DOWN ARROW

      // [71..72]
      // code, ctrl, shft,  alt, none,
      { 0x169, 0x03, 0x03, 0x03, 0x03, }, // END (->BREAK)
      { 0x07e, 0x00, 0x00, 0x00, 0x00, }, // SCROLL
#define F10_IDX  73 // index of F10 in the table
      // [73..82]
      // for alt-F10-F9 a second byte of '0'-'9' is output
      // code, ctrl, shft,  alt, none,
      { 0x009, 0x00, 0x00, 0xfc, 0x00, }, // F10
      { 0x005, 0x01, 0x01, 0xfc, 0x01, }, // F1
      { 0x006, 0x02, 0x02, 0xfc, 0x02, }, // F2
      { 0x004, 0x04, 0x04, 0xfc, 0x04, }, // F3
      { 0x00c, 0x0c, 0x0c, 0xfc, 0x0c, }, // F4
      { 0x003, 0x15, 0x15, 0xfc, 0x15, }, // F5
      { 0x00b, 0x10, 0x10, 0xfc, 0x10, }, // F6
      { 0x083, 0x0e, 0x0e, 0xfc, 0x0e, }, // F7
      { 0x00a, 0x13, 0x13, 0xfc, 0x13, }, // F8
      { 0x001, 0x1a, 0x1a, 0xfc, 0x1a, }, // F9
   };

uint8_t ps2_caps = 0;


void main(void)
   {
   __disable_interrupt();
#if USE_WDT
   WDTCTL = WDTPW + WDTCNTCL;
#else
   // Stop watchdog timer
   WDTCTL = WDTPW + WDTHOLD;
#endif

   // Calibrate the clock for 8MHz
   DCOCTL = 0;
#if 1
   BCSCTL1 = CALBC1_8MHZ != 0xFF ? CALBC1_8MHZ : 0x8d;
   DCOCTL  = CALDCO_8MHZ != 0xFF ? CALDCO_8MHZ : 0x58;
#else
   BCSCTL1 = CALBC1_8MHZ;
   DCOCTL  = CALDCO_8MHZ;
#endif
   P2SEL = 0x00;

   // Configure and initialize ports
#if 0
   // enable pullups on ps2 clk and dta and uart rx
   // enable pulldown on keyboard busy
   // output keyboard dta high, uart tx high
   // output keyboard clock low
   P1OUT = PS2_CLK | PS2_DTA | KBD_DTA | UART_TX | UART_RX;
#else
   // enable pullups on ps2 clk, dta and keyboard busy and uart rx
   // output keyboard dta high, uart tx high
   // output keyboard clock low
   P1OUT = PS2_CLK | PS2_DTA | KBD_BSYN | KBD_DTA | UART_TX | UART_RX;
#endif
   P1REN = PS2_CLK | PS2_DTA | KBD_BSYN | UART_RX;
   P1DIR = KBD_CLK | KBD_DTA | UART_TX;  // keyboard clk and dta outputs
   P1IES = PS2_CLK;  // falling edge
   P1IE  = PS2_CLK;

   // Initialize LED0 off, LED1 on
   P2OUT = LED1;
   P2DIR = LED0 | LED1;

   // Initialize the timer - channel0 for the keyboard bit rate
   TACCR0 = PERIOD0 - 1; // Up mode counts to TACCR9
   TACTL = TACLR | TASSEL_2 | MC_1 | TAIE; // Set the timer A to SMCLCK, Up mode, Overflow interrupt

   // Enable global interrupts
   __enable_interrupt();

   uint8_t ps2_dta = 0, prev_ps2_dta = 0, prev_prev_ps2_dta = 0;
   uint8_t ps2_shft = 0, ps2_ctrl = 0, ps2_alt = 0;
   while(1)
      {
#if USE_WDT
      WDTCTL = WDTPW + WDTCNTCL;
#endif

      if(ps2_hld_rdy)
         {
         prev_prev_ps2_dta = prev_ps2_dta;
         prev_ps2_dta = ps2_dta;
         ps2_dta = ps2_hld;
         ps2_hld_rdy = 0;

         if(!(ps2_dta == 0xf0 || ps2_dta == 0xe0))
            {
            uint8_t ps2_esc = (prev_ps2_dta == 0xf0) ? (prev_prev_ps2_dta == 0xe0) : (prev_ps2_dta == 0xe0);
            uint16_t ps2_esc_dta = (ps2_esc << 8) | ps2_dta;
            uint8_t ps2_release = (prev_ps2_dta == 0xf0);
            if(!ps2_release)
               {
               uint8_t ps2_code = 0;
               uint8_t ps2_code_rdy = 0;
               uint16_t i;

               for(i = 0; i < sizeof(ps2_map)/sizeof(ps2_map[0]); i++)
                  {
                  if(ps2_esc_dta == ps2_map[i].code)
                     {
                     ps2_code = ps2_ctrl ? ps2_map[i].ctrl : ps2_alt ? ps2_map[i].alt : ps2_shft ? ((i < 26 && (ps2_caps & 1)) ? ps2_map[i].none : ps2_map[i].shft)
                                                                                                 : ((i < 26 && (ps2_caps & 1)) ? ps2_map[i].shft : ps2_map[i].none);
                     ps2_code_rdy = 1;
                     break;
                     }
                  }
               if(ps2_code_rdy)
                  {
                  uint8_t tmp_buf_in = kbd_buf_in;
                  kbd_buf[tmp_buf_in] = ps2_code;
                  tmp_buf_in = (tmp_buf_in + 1) & (KBD_BUF_N - 1);
                  if(tmp_buf_in != kbd_buf_out)
                     {
                     if(i >= F10_IDX && i < F10_IDX + 10 && ps2_code == 0xfc)
                        {
                        kbd_buf[tmp_buf_in] = '0' + (i - F10_IDX);
                        tmp_buf_in = (tmp_buf_in + 1) & (KBD_BUF_N - 1);
                        if(tmp_buf_in != kbd_buf_out)
                           kbd_buf_in = tmp_buf_in;
                        }
                     else
                        kbd_buf_in = tmp_buf_in;
                     }
                  }
               else
                  {
                  if(ps2_esc_dta == 0x012) // LEFT SHIFT
                     ps2_shft |= 1;
                  else if(ps2_esc_dta == 0x059) // RIGHT SHIFT
                     ps2_shft |= 2;
                  else if(ps2_esc_dta == 0x014) // LEFT CTRL
                     ps2_ctrl |= 1;
                  else if(ps2_esc_dta == 0x114) // RIGHT CTRL
                     ps2_ctrl |= 2;
                  else if(ps2_esc_dta == 0x011) // LEFT ALT
                     ps2_alt  |= 1;
                  else if(ps2_esc_dta == 0x111) // RIGHT ALT
                     ps2_alt  |= 2;
                  else if(ps2_esc_dta == 0x058) // CAPS
                     ps2_caps = 2 | ((ps2_caps >> 1) ^ 1 ^ ps2_caps);
                  }
               }
            else
               {
               if(ps2_esc_dta == 0x012) // LEFT SHIFT
                  ps2_shft &= 2;
               else if(ps2_esc_dta == 0x059) // RIGHT SHIFT
                  ps2_shft &= 1;
               else if(ps2_esc_dta == 0x014) // LEFT CTRL
                  ps2_ctrl &= 2;
               else if(ps2_esc_dta == 0x114) // RIGHT CTRL
                  ps2_ctrl &= 1;
               else if(ps2_esc_dta == 0x011) // LEFT ALT
                  ps2_alt  &= 2;
               else if(ps2_esc_dta == 0x111) // RIGHT ALT
                  ps2_alt  &= 1;
               else if(ps2_esc_dta == 0x058) // CAPS
                  ps2_caps &= 1;
               }
            }
         }
      }
   }


// Timer A0 interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A0 (void)
   {
   while(1)
      {
      }
   }

// Timer A1 interrupt service routine (shared interrupt)
#pragma vector=TIMER0_A1_VECTOR
__interrupt void Timer_A1 (void)
   {
   static uint8_t kbd_state = 0;
   static uint8_t kbd_shr;
   static uint16_t div = BAUD*2L - 1;  // 1/2 second
   TAIV;

   if(kbd_state == 0)
      {
      // if character is ready in fifo and computer is ready then send next character
      if(kbd_buf_out != kbd_buf_in && (P1IN & KBD_BSYN))
         {
         kbd_shr = kbd_buf[kbd_buf_out];
         kbd_buf_out = (kbd_buf_out + 1) & (KBD_BUF_N - 1);
         P1OUT &= ~UART_TX;  // output start bit
         P2OUT |= LED1;
         ++kbd_state;
         }
      }
   else if(kbd_state < 4)
      {
      // delay for tandy key out
      // start bit for uart out
      ++kbd_state;
      }
   else
      {
      // the 8 data bits are output for states 4-7, 8-11, ..., 32-35
      // states 36-39 are end of data pulse for tandy key out
      // states 36-39 are stop bit for uart out
      switch(kbd_state & 3)
         {
         case 0:
            if(kbd_state < 36)
               {
               // states 4-35 are data bits
               if(kbd_shr & 1)
                  {
                  P1OUT |= (KBD_DTA | UART_TX);
                  }
               else
                  {
                  P1OUT &= ~(KBD_DTA | UART_TX);
                  }
               kbd_shr >>= 1;
               }
            else
               {
               // states 36-39 are end of data pulse for tandy key out
               // states 36-39 are stop bit for uart out
               //P1OUT &= ~KBD_DTA;  // output low for beginning of end of data pulse
               //P1OUT |= UART_TX;  // output high for stop bit
               P1OUT = P1OUT & ~KBD_DTA | UART_TX;
               }
            break;
         case 1:
            // tandy key clock remains low during end of data pulse
            if(kbd_state < 36)
               P1OUT |= KBD_CLK;
            break;
         case 2:
            // output tandy key data high
            // this will latch the clock level into the end of data flipflop
            // if clock is high then this is data bit
            // if clock is low then this is end of data
            P1OUT |= KBD_DTA;
            break;
         case 3:
            // return clock low always
            P1OUT &= ~KBD_CLK;
            break;
         }

      if(!(++kbd_state < 40))
         {
         P2OUT &= ~LED1;
         kbd_state = 0;
         }
      }

   // toggle the led
   if(div-- == 0)
      {
      P2OUT ^= LED0;
      // slow toggle for capslock off
      // fast toggle for capslock on
      div = (ps2_caps & 1) ? (BAUD - 1) : (BAUD*2L - 1);
      }
   }

// Timer A1 interrupt service routine (shared interrupt)
#pragma vector=PORT1_VECTOR
__interrupt void Port1 (void)
   {
   static uint16_t ps2_shr = 0;
   P1IFG = 0x00;

   ps2_shr = (ps2_shr >> 1) | ((P1IN & PS2_DTA) ? 0 : 0x400);
   if(ps2_shr & 0x1)
      {
      if(!(ps2_shr & 0x400))
         {
         ps2_hld = (~ps2_shr) >> 1;
         ps2_hld_rdy = 1;
         }
      ps2_shr = 0;
      }
   }
