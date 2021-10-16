  /**********************************************************************************************
  *                                                                                             *
  *           ������� ������� ���� �� Hyundai Sonata EF (������� � �������� ������)             *
  *                                                                                             *
  *                                   ATtiny13A     0.6 MHz                                     *
  *                                                                                             *
  **********************************************************************************************/


  /**********************************************************************************************
  *                              ���������� ���������� � �������                                *
  **********************************************************************************************/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

#define LAMP                            0         // ����� �� ����� (PB0)
#define BTN                             1         // ������ (PB1)
#define Control_1                       2         // ����������� ��� (�����, ����) (PB2)
#define Control_2                       3         // ����������� ��� (������) (PB3)
#define LED_indicator                   4         // ��������� ������ � ����������� ��� (PB4)
#define Light_Duty_Def                  52        // ���������� �� ���������                        (52 / 256 = 20%)
#define Time_Smooth                     2960      // ����� �������� ������� (2500 ms * 1.18 = 2960) (1.18 = 4/3.4 ����������� ����., ����� ���� �� ����� � �������) (����� �����, ��� ������� �� 4 �� ���������� ����� � CountSmoothCalc())
#define Time_Delay_Before_Stop          1470      // �������� ����� �����������                     (1470 * 3.4 ms = 5 sec)
#define Time_Ctrl_2_Countdown           882       // ���������� ����� ��� �������                   (882 * 3.4 ms = 3 sec)
#define Times_for_Ctrl_2                3         // ����������� ���������� ��������� ������� �� ���������� �����
#define Count_max_btn                   118       // max �������� �������� (����������� ������)     (118 * 3.4 ms = 400 ms)
#define _smooth_is_need                 0         // ���� ����������� �������� ������� (��� 0)
#define _DRL_started                    1         // ���� ����������� ��� (��� 1)
#define _btn_pressed                    2         // ���� ������� ������ (��� 2)
#define _delay_in_effect                3         // ���� ����������� �������� ���������� (��� 3)
#define _ctrl_2_countdown               4         // ���� ������� ������� ��� Control_2 (��� 4)
#define _ctrl_2_switch                  5         // ���� ������� (������� �������) ��� Control_2 (��� 5)
#define _DRL_off_by_ctrl_2              6         // ���� ����������� �������� ���
#define EE_byte_Set_1                   1         // ����� ������ EEPROM - ��������� ������ �1 (8 ���)
#define EE_byte_Set_2                   2         // ����� ������ EEPROM - ��������� ������ �2 (8 ���)
#define EE_byte_Init                    3         // ����� ������ EEPROM - ������ ������������� (8 ���)

volatile uint8_t    Flag_Byte           = 0;      // �������� ����������
volatile uint8_t    Count_Time          = 0;      // ������� (���������������� �� ������������ �������)
volatile uint16_t   Count_Delay_Stop    = 0;      // ������� ��� �������� (���������������� �� ������������ �������)
volatile uint16_t   Count_Delay_Ctrl_2  = 0;      // ������� ��� ��������� Control_2
volatile uint8_t    Count_Times_Ctrl_2  = 0;      // ������� ������������ �������
volatile uint8_t    Delay_Smooth;                 // �������� ��� �������� �������
volatile uint8_t    Light_Duty;                   // ����������

void Presets(void);
void SmoothIgnition(void);
void StartDRL(void);
void StopDRL(void);
void LoadData(void);
void SaveData(void);
void ChangeBrightness(void);
void DelayBeforeStop(void);
void SettingsInit(void);
void CountSmoothCalc(void);


  /**********************************************************************************************
  *                                     �������� �������                                        *
  **********************************************************************************************/

int main (void)
{
  Presets();

  while (1)
  {
    if ((PINB & (1<<Control_1)) && (~Flag_Byte & (1<<_DRL_off_by_ctrl_2)))   // ���� �� ���� "1" � ��� �� ��������� �������� - ���������
    {
      if (~Flag_Byte & (1<<_DRL_started))                       // ���� ��� ���������
      {
        StartDRL();
      }
      else
      {
        if (Flag_Byte & (1<<_delay_in_effect))                  // ���� ���� ������������ �������� �� ����������
        {
          Flag_Byte &=~ (1<<_delay_in_effect);                  // �������� ��������
        }
      }
      
      if (Flag_Byte & (1<<_smooth_is_need))                     // ���� ����� ������� ������
      {
        SmoothIgnition();
      }
    }
    else                                                        // ���� �� ���� "0" - ����������
    {
      if (Flag_Byte & (1<<_DRL_started))
      {
        DelayBeforeStop();                                      // ��������� �������� � ����������� �����������
      }
    }
  
    if ((Flag_Byte & (1<<_btn_pressed)) && (Count_Time >= Count_max_btn))    // ������ ������ � ��������� �����������
    {
      PORTB     &=  ~(1<<LED_indicator);                        // ����. ���������
      Flag_Byte &=  ~(1<<_btn_pressed);                         // ����� ����� ������� ������
      ChangeBrightness();
      SaveData();
      GIMSK     |=  (1<<INT0);                                  // ��������� ���������� INT0
    }
    
    if ((Flag_Byte & (1<<_ctrl_2_switch)) && (Count_Time >= Count_max_btn))  // ������ � ������� � ��������� �����������
    {
      Flag_Byte &=  ~(1<<_ctrl_2_switch);                       // ����� ����� ������������ �������
      Count_Times_Ctrl_2++;                                     // ������������ ������� +1
      GIMSK     |=  (1<<PCIE);                                  // ��������� ���������� PCINT0
    }
  
    if ((Flag_Byte & (1<<_ctrl_2_countdown)) && (Count_Delay_Ctrl_2 >= Time_Ctrl_2_Countdown)) // ���� ������ �������������� ������� �������
    {
      if ((Count_Times_Ctrl_2 >= Times_for_Ctrl_2) && (PINB & (1<<Control_2)))  // ���� ����������� ���������� ������������ ���������� � ������� ������
      {
        if (~Flag_Byte & (1<<_DRL_off_by_ctrl_2))               // ���� �������� ��� �� ���������
        {
          Flag_Byte |=  (1<<_DRL_off_by_ctrl_2);                    
          PORTB     |=  (1<<LED_indicator);
          
          if (Flag_Byte & (1<<_DRL_started))                    // ���� ��� ��������
          {
            StopDRL();
          }
        } 
        else                                                    // ���� �������� ��� ���������
        {
          Flag_Byte &= ~(1<<_DRL_off_by_ctrl_2);
          PORTB     &= ~(1<<LED_indicator);
        }
      }
      
      Count_Times_Ctrl_2 = 0;                                   // ��������� �������� ���������� ��������� �������
      Flag_Byte &=  ~(1<<_ctrl_2_countdown);                    // ���� ����� ������� ��� ��������� Control_2
    }
    
    asm("nop");
  }
}


  /**********************************************************************************************
  *                                    ��������� �������                                        *
  **********************************************************************************************/

    /*---------------------------------- ������������� --------------------------------------*/

void Presets (void)
{
  DDRB      |=  (1<<LAMP) | (1<<LED_indicator);                 // ��������� ������
  DDRB      &=  ~((1<<BTN) | (1<<Control_1) | (1<<Control_2));
  PORTB     |=  (1<<BTN) | (1<<Control_1) | (1<<Control_2);
  Flag_Byte |=  (1<<_smooth_is_need);                           // ��������� �������� �������
  
  MCUCR     |=  (1<<ISC01);                                     // ���������� INT0 �� ������������ �������
  
  PCMSK     |=  (1<<PCINT3);                                    // ���������� �� �������� ������ �� ���� ��3
  GIMSK     |=  (1<<PCIE);                                      // ��������� ����������
  
  TCCR0A    |=  (1<<WGM00) | (1<<WGM01);                        // ����� ������ ������ ������� (����. 11.8 ���. 73)        Fast PWM (3)
  TCCR0B    |=  (1<<CS01);                                      // ��������� ������������ ������� (����. 11.9 ���. 74)     8 (0.6 MHz/256/8=293 Hz (3.4 ms))
  OCR0A     =   1;                                              // ��������� �������� ��� �������� (11.9.4 ���. 75)       (����������)
  TIMSK0    |=  (1<<TOIE0);                                     // ��������� ���������� �� ������������ ��������
  
  SettingsInit();
  LoadData();
  CountSmoothCalc();
  
  asm ("sei");                                                  // ���������� ���������� (���. 161)
}

    /*----------------------------- ������������� �������� ---------------------------------*/

void SettingsInit (void)
{
  if (eeprom_read_byte((uint8_t*)EE_byte_Init) != 55)           // 55 - ��������� �����
  {
    eeprom_write_byte((uint8_t*)EE_byte_Set_1, Light_Duty_Def); // ������ � ������ ������������������ ��������
    eeprom_write_byte((uint8_t*)EE_byte_Set_2, Light_Duty_Def);
    Light_Duty = Light_Duty_Def;
    eeprom_write_byte((uint8_t*)EE_byte_Init, 55);              // ������ � ������ 55 - ��������� �������������
  } 
}

    /*--------------------------------- ������� ������ -------------------------------------*/

void SmoothIgnition (void)
{
  if (Count_Time >= Delay_Smooth)                               // ����� ������� ������ ������� ��������
  {
    if (OCR0A < Light_Duty)                                     // ���� ���������� ��� �� �������� ������ ������
    {
      OCR0A++;                                                  // ���������� ����������
      Count_Time = 0;
    }
    else                                                        // ���� ���������� �������� ������ ������
    {
      Flag_Byte &= ~(1<<_smooth_is_need);                       // ��������� �������� �������
    }
  } 
}

    /*---------------------------------- ��������� ��� --------------------------------------*/

void StartDRL(void)
{
  asm ("cli");
  Flag_Byte |=  (1<<_DRL_started);                              // ��������� ����� ����������� ���
  TCCR0A    |=  (1<<COM0A1);                                    // ����������� ���� OC0A (PB0) � ������ ���-����������
  GIMSK     |=  (1<<INT0);                                      // ��������� ���������� INT0
  asm ("sei");
} 

    /*---------------------------------- ���������� ��� -------------------------------------*/

void StopDRL(void)
{
  asm ("cli");
  Flag_Byte &=  ~(1<<_DRL_started);                             // ����� ����� ����������� ���
  TCCR0A    &=  ~(1<<COM0A1);                                   // ���������� ���� OC0A (PB0) �� ������ ���-����������
  PORTB     &=  ~(1<<LAMP);                                     // ���������� ����� ������ �� �����
  OCR0A     =   1;                                              // ��������� �������� ��� �������� (��� �������� �������)
  Flag_Byte |=  (1<<_smooth_is_need);                           // ��������� �������� ������� (��� ������������ ���������)
  Flag_Byte &=  ~(1<<_delay_in_effect);                         // ���������� �������� ����� �����������
  GIMSK     &=  ~(1<<INT0);                                     // ���������� ���������� INT0
  asm ("sei");
}

    /*---------------------------- �������� ����� ����������� -------------------------------*/

void DelayBeforeStop(void)
{
  if (~Flag_Byte & (1<<_delay_in_effect))                       // ���� �������� ��� �� ������������
  {
    Flag_Byte |= (1<<_delay_in_effect);                         // ��������� ��������
    Count_Delay_Stop = 0;                                       // ����� �������� ��������
  }
  
  if (Count_Delay_Stop >= Time_Delay_Before_Stop)               // ���� ������� �������� �� �������������� ��������
  {
    StopDRL();
  }
}

    /*--------------------------------- �������� ������ -------------------------------------*/

void LoadData(void)
{
  if (eeprom_read_byte((uint8_t*)EE_byte_Set_1) == eeprom_read_byte((uint8_t*)EE_byte_Set_2)) // ���� �������� � 2-� ������� ���������
  {
    Light_Duty = eeprom_read_byte((uint8_t*)EE_byte_Set_1);     // ������ �� ������ ������������ �������� ����������
  }
  else                                                          // ���� ��������� ���� � � ������� ������ ������
  {
    eeprom_write_byte((uint8_t*)EE_byte_Init, 0);               // ����� ������ �������������
    SettingsInit();                                             // ����� ��������
  }
}

    /*-------------------------------- ���������� ������ ------------------------------------*/

void SaveData(void)
{
  eeprom_write_byte((uint8_t*)EE_byte_Set_1, Light_Duty);       // ������ � ������ ������ ��������
  eeprom_write_byte((uint8_t*)EE_byte_Set_2, Light_Duty);
}

    /*-------------------------------- ��������� ������� ------------------------------------*/

void ChangeBrightness(void)
{
  if (Light_Duty <= 97)                                         // ������������� �������� �� ���� �������� ���������� - 25, 34, 43, 52, 61, 70, 79, 88, 97, 106
  {
    Light_Duty += 9;                                            // �� 25 �� 106 � ����� 9 = 10 �������� ������� (10%...41%)
  }
  else
  {
    Light_Duty = 25;                                            // ������ �������� �� ���� �������� ����������
  }
  
  OCR0A = Light_Duty;
  CountSmoothCalc();
}

    /*-------------------------- ������ �������� �������� ������� ---------------------------*/

void CountSmoothCalc(void)
{
  Delay_Smooth = Time_Smooth / Light_Duty / 4;
}


  /**********************************************************************************************
  *                                         ����������                                          *
  **********************************************************************************************/

    /*----------------------------- �� ������������ �������� --------------------------------*/

ISR (TIM0_OVF_vect)                                             // ������ 3.4 ��
{
  Count_Time++;
  Count_Delay_Stop++;
  Count_Delay_Ctrl_2++;
}

    /*---------------------------- �� ��������� ��������� ����� -----------------------------*/

ISR (INT0_vect)                                                 // �� ���������� ������� INT0 
{
  if (~PINB & (1<<BTN))                                         // ������� ��������, �.�. ���������� ����������� �� ������ �� ���������� ������� (� �������� �� �������� �������� � ��� �������)
  {
    GIMSK       &=  ~(1<<INT0);                                 // ������ ���������� INT0
    Count_Time  =   0;                                          // ����� ��������
    Flag_Byte   |=  (1<<_btn_pressed);                          // ��������� ����� ������� ������
    PORTB       |=  (1<<LED_indicator);                         // ���. ���������
  }
}

    /*---------------------------- �� ��������� ��������� ����� -----------------------------*/

ISR (PCINT0_vect)                                               // �� ��������� ������ PCINT0
{
  if (~PINB & (1<<Control_2))                                   // �� ������� ������ PB3
  {
    GIMSK       &=  ~(1<<PCIE);                                 // ������ ���������� PCINT0
    Count_Time  =   0;                                          // ����� �������� (��� ������������)
    
    if (~Flag_Byte & (1<<_ctrl_2_countdown))                    // ���� ������ ��� �� ������� (������ ������������ �������)
    {
      Count_Delay_Ctrl_2 = 0;                                   // ����� �������� ������� �������
      Flag_Byte   |=  (1<<_ctrl_2_countdown);                   // ��������� ����� ������ ������� ��� ��������� Control_2
    }
    
    Flag_Byte   |=  (1<<_ctrl_2_switch);                        // ��������� ����� ����������� ������� Control_2
  }
}
