/*
  Arduino based PIC programmer
 */
 
#include <TimerOne.h>
#include <debugf.h>

int led_pin = 6;
int led2_pin = 7;
int pump_pin = 19; // Must be Timer1 PWM output A
int reset_pin = 0;
int program_pin = 1;
int clock_pin = 2;
int data_pin = 3;
int button_pin = 8;

long delay_times[21];

static bool attached;
static bool data_active;


#define RESET_DELAY_MS		10

typedef enum {
    MRST_RELEASE,
    MRST_RESET,
    MRST_PROGRAM
} mrst_state;

static void
set_mrst(mrst_state s)
{
  switch (s)
    {
    case MRST_RELEASE:
      digitalWrite(program_pin, 0);
      digitalWrite(reset_pin, 0);
      break;

    case MRST_RESET:
      digitalWrite(reset_pin, 1);
      digitalWrite(program_pin, 0);
      break;

    case MRST_PROGRAM:
      digitalWrite(program_pin, 1);
      digitalWrite(reset_pin, 0);
      break;
    }
}

static inline void
clock_low(void)
{
  digitalWrite(clock_pin, 0);
}

static inline void
clock_high(void)
{
  digitalWrite(clock_pin, 1);
}

static inline void
data_out(int level)
{
  digitalWrite(data_pin, level);
}

static uint8_t
data_in(void)
{
  return digitalRead(data_pin);
}

static inline void
data_tristate(void)
{
  if (!data_active)
    return;
  pinMode(data_pin, INPUT);
  data_active = false;
}

static inline void
data_enable(void)
{
  if (data_active)
    return;
  pinMode(data_pin, OUTPUT);
  data_active = true;
}

static void
attach_device(void)
{
  set_mrst(MRST_RESET);
  data_enable();
  clock_low();
  data_out(0);
  delay(RESET_DELAY_MS);
  set_mrst(MRST_PROGRAM);
  delayMicroseconds(5);
  attached = true;
}

static void
detach_device(void)
{
  if (!attached)
    return;
  set_mrst(MRST_RESET);
  data_tristate();
  delay(RESET_DELAY_MS);
  set_mrst(MRST_RELEASE);
  attached = false;
}

static inline void
hold(void)
{
  asm volatile ("nop");
}

static inline void
out_bit(uint8_t level)
{
  clock_high();
  data_out(level);
  hold();
  clock_low();
  hold();
}


static void
out8(uint8_t val)
{
  data_enable();
  out_bit((val & 0x01) != 0);
  out_bit((val & 0x02) != 0);
  out_bit((val & 0x04) != 0);
  out_bit((val & 0x08) != 0);
  out_bit((val & 0x10) != 0);
  out_bit((val & 0x20) != 0);
  out_bit((val & 0x40) != 0);
  out_bit((val & 0x80) != 0);
}

static void
out6(uint8_t val)
{
  data_enable();
  out_bit((val & 0x01) != 0);
  out_bit((val & 0x02) != 0);
  out_bit((val & 0x04) != 0);
  out_bit((val & 0x08) != 0);
  out_bit((val & 0x10) != 0);
  out_bit((val & 0x20) != 0);
}

static uint8_t
in8(void)
{
  uint8_t val = 0;
  uint8_t mask;

  data_tristate();
  for (mask = 1; mask; mask <<= 1)
    {
      clock_high();
      hold();
      if (data_in())
	val |= mask;
      clock_low();
      hold();
    }
  return val;
}

static bool
is_hex_digit(char c)
{
  return (c >= '0' && c <= '9')
	 || (c >= 'a' && c <= 'f')
	 || (c >= 'A' && c <= 'F');
}

static uint8_t
from_hex(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c + 10 - 'a';
  if (c >= 'A' && c <= 'F')
    return c + 10 - 'A';
  return 0;
}

static char last_char;

static char
next_char(void)
{
  char prev;
again:
  while (!Serial.available())
    {
      if (!Serial)
	{
	  last_char = 0;
	  return 0;
	}
    }
  prev = last_char;
  last_char = Serial.read();
  if (prev == '\r' && last_char == '\n')
    goto again;
  if (last_char == '\r')
    return '\n';
  return last_char;
}

static inline void
ok(void)
{
  Serial.write('\n');
}

static inline void
error(void)
{
  Serial.print("!\n");
  detach_device();
  while (last_char && last_char != '\r' && last_char != '\n')
    next_char();
}

static void
hex_response(uint8_t val)
{
  char c;
  c = val >> 4;
  if (c < 10)
    c += '0';
  else
    c += 'a' - 10;
  Serial.write(c);
  c = val & 0xf;
  if (c < 10)
    c += '0';
  else
    c += 'a' - 10;
  Serial.write(c);
}

static void
do_delay(void)
{
  long val;
  char c;
  int n;

  c = next_char();
  if (c == '.')
     n = 20;
  else if (c >= 'g' && c <= 'z')
    n = c - 'g';
  else
    {
      error();
      return;
    }
  val = 0;
  while (true)
    {
      c = next_char();
      if (c == '\n')
	break;
      if (is_hex_digit(c))
	val = (val << 4) | from_hex(c);
      else
	{
	  error();
	  return;
	}
    }
  delay_times[n] = val;
  ok();
}

static int
read_hex8(void)
{
  char c;
  uint8_t val;

  c = next_char();
  if (!is_hex_digit(c))
    {
      error();
      return -1;
    }
  val = from_hex(c) << 4;
  c = next_char();
  if (!is_hex_digit(c))
    {
      error();
      return -1;
    }
  val |= from_hex(c);
  return val;
}

static void
do_cmd(void)
{
  uint8_t val;
  int test;
  int mask;
  char c;

  if (!attached)
    {
      error();
      return;
    }
  while (true)
    {
      c = next_char();
//debugf("#char %d\n", c);
      if (c == '.')
	{
	  delayMicroseconds(delay_times[20]);
//debugf("#delay.\n");
	}
      else if (c >= 'g' && c <= 'z')
	{
	  delayMicroseconds(delay_times[c - 'g']);
//debugf("#delay%c\n", c);
	}
      else if (c == '<')
	{
//debugf("#read\n");
	  hex_response(in8());
	}
      else if (c == '=')
	{
	  val = in8();
	  test = read_hex8();
	  if (test == -1)
	    return;
	  mask = read_hex8();
	  if (mask == -1)
	    return;
//debugf("#got %x want %x mask %x\n", val, test, mask);
	  if (((val ^ test) & mask) != 0)
	    {
	      Serial.write('~');
	      error();
	      return;
	    }
	}
      else if (c == '*')
	{
	  val = read_hex8();
	  if (val == -1)
	    return;
//debugf("#cmd %x\n", val);
	  out6(val);
	}
      else if (is_hex_digit(c))
	{
	  val = from_hex(c) << 4;
	  c = next_char();
	  if (!is_hex_digit(c))
	    {
	      error();
	      return;
	    }
	  val |= from_hex(c);
//debugf("#write %x\n", val);
	  out8(val);
	}
      else if (c == '\n')
	break;
      else
	{
	  error();
	  return;
	}
    }
  ok();
}

static void
do_attach(void)
{
  if (next_char() != '\n')
    {
      error();
      return;
    }
  attach_device();
  ok();
}

static void
do_detach(void)
{
  if (next_char() != '\n')
    {
      error();
      return;
    }
  detach_device();
  ok();
}

static void
do_version()
{
  int ver;

  ver = read_hex8();
  if (ver != 0 || next_char() != '\n')
    {
      error();
      return;
    }
  Serial.print("00");
  ok();
}

static void
do_help()
{
  if (next_char() != '\n')
    {
      error();
      return;
    }
  Serial.print("ArduPIC 1.0\n");
  ok();
}

void loop()
{
  char c;

  while (!Serial)
    delay(1);

  c = next_char();
//  if (c != 'V')
//debugf("#Command %c\n", c);
  switch (c)
    {
    case 'T':
      do_delay();
      break;

    case 'A':
      do_attach();
      break;

    case 'C':
      do_cmd();
      break;

    case 'D':
      do_detach();
      break;

    case '\n':
      ok();
      break;

    case 'V':
      do_version();
      break;

    case '?':
      do_help();
      break;

    default:
      error();
      break;
    }
//debugf("#Command done\n");
}

void setup()
{
  Serial.begin(9600);

  pinMode(led_pin, OUTPUT);     
  pinMode(led2_pin, OUTPUT);     
  pinMode(pump_pin, OUTPUT);     
  pinMode(reset_pin, OUTPUT);     
  pinMode(program_pin, OUTPUT);     
  pinMode(clock_pin, OUTPUT);     
  pinMode(button_pin, INPUT);     

  digitalWrite(led_pin, 1);
  digitalWrite(led2_pin, 1);
  digitalWrite(program_pin, 0);
  digitalWrite(reset_pin, 0);

  // Enable charge pump (high frequency PWM)
  Timer1.initialize(10);
  Timer1.pwm(1, 512);
}

