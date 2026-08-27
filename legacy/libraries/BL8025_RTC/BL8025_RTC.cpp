#include "BL8025_RTC.h"
#include <Wire.h>
#include <TimeLib.h>

/********************************************************************************/
BL8025_RTC::BL8025_RTC(){
  
}

bool BL8025_RTC::write(tmElements_t tm){
  _year       = tmYearToCalendar(tm.Year);
  _month      = tm.Month;
  _day        = tm.Day;
  _hour       = tm.Hour;
  _minute     = tm.Minute;
  _second     = tm.Second;
  return _begin();  
}

tmElements_t BL8025_RTC::read(bool lx){
  tmElements_t tm;
  if(_read(lx)){
    tm.Year = CalendarYrToTm(_year);
    tm.Month = _month;
    tm.Day = _day;
    tm.Hour = _hour;
    tm.Minute = _minute;
    tm.Second = _second;
    return tm;
  }
  return tm;
}

/********************************************************************************/
bool BL8025_RTC::_begin() //BL8025T具有地址自动增加功能，只需要在第一次时写入寄存器的地址，之后就可以一直写入数据
{
  byte n;
  
  //RX8025T
  Wire.beginTransmission(RTC8052_I2CADDR); //开始指令，条件：器件的7位地址
  Wire.write(0x00);         //首次写入寄存器的地址到8025T 0x00表示从秒开始
  n = _toBCD(_second);
  Wire.write(n);            //0:Seconds 秒
  n = _toBCD(_minute); 
  Wire.write(n);            //1:Minutes 分
  n = _toBCD(_hour);
  Wire.write(n);            //2:Hours 时
  Wire.write(0x00);         //3:Weekdays 星期 星期一-星期天：12345670
  n = _toBCD(_day);
  Wire.write(n);            //4:Days 天
  n = _toBCD(_month);	
  Wire.write(n);            //5:Months 月
  n = _toBCD(_year);
  Wire.write(n);            //6:Years 年
  Wire.endTransmission();   //结束指令
  delay(1);
    
  //剩下的用不上全写0
  Wire.beginTransmission(RTC8052_I2CADDR);
  Wire.write(0x08);   //从寄存器8开始，原RX8025T-8E
  Wire.write(0x00);   //8: MIN Alarm
  Wire.write(0x00);   //9: HOUR Alarm
  Wire.write(0x00);   //A: WEEK Alarm DAY Alarm
  Wire.write(0x00);   //B: Timer Counter 0
  Wire.write(0x00);   //C: Timer Counter 1
  Wire.write(0x00);   //D: Extension Register
  Wire.write(0x00);   //E: Flag Register
  Wire.write(0x00);   //F: Control Register
  Wire.endTransmission();
  delay(1);
  
  return true;
}

bool BL8025_RTC::_read(bool lx)
{
  byte years,months,days,hours,minutes,seconds;
	
  Wire.requestFrom(RTC8052_I2CADDR, 16);//RX8025一次给出16个字节
  /*
	由主设备用来向从设备请求字节。
	请求发送之后可以使用available（）和read（）来接受并读取数据。
	从Arduino 1.0.1开始，requestFrom（）接受一个布尔参数来适配某些I2C设备来达到兼容的目的。
	如果为true，则requestFrom（）在请求之后发送停止消息，从而释放I2C总线。
	如果为false，则requestFrom（）在请求之后发送重启消息。 总线不会释放，这个操作就阻止了另一个主设备在消息之间请求。
	这样一来，一台主设备就可以在控制下发送多个请求。
	默认值是true。
	*/
	if(lx) //RX8025T,读取到的数据偏移的8位
	{
		
		for(uint8_t i=0;i<8;i++) //前8位用不上跳过
		{
			Wire.read();
		}
		seconds = Wire.read();  // 秒 
		minutes = Wire.read();  // 分
		hours = Wire.read();    // 时
		Wire.read();            // WEEK 星期 0-星期天 1-6（星期1到星期6）
		days = Wire.read();     // 天
		months = Wire.read();   // 月
		years = Wire.read();    // 年
		while (Wire.available()) //剩下的用不上跳过
		{
			Wire.read();
		}
	}
	else  //BL8025T
	{
		seconds = Wire.read();  // 秒 
		minutes = Wire.read();  // 分
		hours = Wire.read();    // 时
		Wire.read();            // WEEK 星期 0-星期天 1-6（星期1到星期6）
		days = Wire.read();     // 天
		months = Wire.read();   // 月
		years = Wire.read();    // 年
		while (Wire.available()) //剩下的用不上跳过
		{
			Wire.read();
		}
	}
  
  //原始数据测试
   /*while (Wire.available()) 
  {
	Serial.print(Wire.read()); Serial.print(" ");
  }
  Serial.println("");*/
  //Serial.print("debug-->");
  //Serial.print(hours >> 4);
  //Serial.print(hours & 0x0F);
  //Serial.print(":");  
  //Serial.print(minutes >> 4);
  //Serial.print(minutes & 0x0F);
  //Serial.print(":");
  //Serial.print(seconds >> 4);
  //Serial.print(seconds & 0x0F);
  //Serial.println("");

 
 
  //CBD1248格式 换算至 十进制
  _year = _fromBCD(years) + 2000;  //手册说年从2000开始计算
  _month = _fromBCD(months);
  _day = _fromBCD(days);
  _hour = _fromBCD(hours);
  _minute = _fromBCD(minutes);
  _second = _fromBCD(seconds);

  return true;
}

/********************************************************************************/
byte BL8025_RTC::_toBCD(int x){
  byte n0,n1;
  n0 = x % 10;
  n1 = (x /10) % 10;
  return (n1 << 4) | n0;
}

int BL8025_RTC::_fromBCD(byte bcd){
  int i0,i1;
  i0 = bcd & 0x0F;
  i1 = (bcd >> 4) * 10;
  return i0 + i1;
}