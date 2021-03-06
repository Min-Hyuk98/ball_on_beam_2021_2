#include <Servo.h> // [2972] 서보 헤더파일 포함

/////////////////////////////
// Configurable parameters //
/////////////////////////////

// Arduino pin assignment
#define PIN_LED 9 // [1234] LED를 아두이노 GPIO 9번 핀에 연결
                  // [2345] ...하면 개선
#define PIN_SERVO 10 // [2951] servo moter를 아두이노 GPIO 10번 핀에 연결
#define PIN_IR A0      // [2961] 적외선센서를 아두이노 A0핀에 연결

// Framework setting
#define _DIST_TARGET 255 //[2967] 탁구공 위치까지의 거리를 255로 고정
         // [2952] 탁구공의 목표 위치를 25.5cm로 설정
#define _DIST_MIN 100 // [2972] 거리 센서가 인식 가능하게 설정한 최소 거리
#define _DIST_MAX 410 // [2972] 거리 센서가 인식 가능하게 설정한 최대 거리

// Distance sensor
#define _DIST_ALPHA 0.9   // [2959] ema 필터에 적용할 알파값

// Servo range
#define _DUTY_MIN 550 // [2952] 서보의 최소 각도값
#define _DUTY_NEU 1388 // [2952] 서보의 중간 각도값 1475
#define _DUTY_MAX 2400 // [1691] 서보의 최대 각도
//#define _DUTY_REAL_MIN 1150 // [2952] 서보의 최소 각도값
#define _DUTY_REAL_MIN 1140
//#define _DUTY_REAL_MAX 1520 // [1691] 서보의 최대 각도
#define _DUTY_REAL_MAX 1480 

// Servo speed control
#define _SERVO_ANGLE 30  //[2967] 서보 각도 설정
#define _SERVO_SPEED 1000  //[2959] 서보의 속도 설정
#define _RAMPUP_TIME 360 // servo speed rampup (0 to max) time (unit: ms)

// Event periods+
#define _INTERVAL_DIST 25  //[2959] 센서의 거리측정 인터벌값
#define _INTERVAL_SERVO 5 //[2967] 서보 INTERVAL값 설정
#define _INTERVAL_SERIAL 100  //[2959] 시리얼 모니터/플로터의 인터벌값 설정

// PID parameters
#define _KP 1.35 // [2957] 비례 제어 값. 서보 각 증가시. 
#define _KD 60
#define _KI 0.0082
// [2964] 실제 거리가 100mm, 400mm일 때 센서가 읽는 값(각 a, b)
#define _ITERM_MAX 50
//////////////////////
// global variables //
//////////////////////

// Servo instance
Servo myservo;  // [2972] 서보 정의

// Distance sensor
float dist_target = _DIST_TARGET; // location to send the ball
float dist_raw, dist_ema;    // [2961] dist_raw : 적외선센서로 얻은 거리를 저장하는 변수
                             // [2961] dist_ema : 거리를 ema필터링을 한 값을 저장하는 변수
float alpha;    // [2959] ema의 알파값을 저장할 변수
float former_reading;
// Event periods
unsigned long last_sampling_time_dist, last_sampling_time_servo, last_sampling_time_serial;// [2957] last_sampling_time_dist : 거리센서 측정 주기
                          // [2957] last_sampling_time_servo : 서보 위치 갱신 주기
                          // [2957] last_sampling_time_serial : 제어 상태 시리얼 출력 주기
bool event_dist, event_servo, event_serial; // [2957] 각각의 주기에 도달했는지를 불리언 값으로 저장하는 변수

// Servo speed control
int duty_chg_per_interval; // [2952] 한 주기당 변화할 서보 활동량을 정의
int duty_target=_DUTY_NEU, duty_curr=_DUTY_NEU; // [2961] 목표위치, 서보에 입력할 위치

// PID variables
float error_curr, error_prev, control, pterm, dterm, iterm;

// for low pass filter
double pre_val = 0;
double pre_time = 0;
double tau = 100;  // 800

// for spike filter
double former = 0;
double former_former = 0;


void setup() {
// initialize GPIO pins for LED and attach servo 
pinMode(PIN_LED, OUTPUT); // [2952] LED를 GPIO 9번 포트에 연결
                          // [2957] LED를 출력 모드로 설정
myservo.attach(PIN_SERVO); // [2952] 서보 모터를 GPIO 10번 포트에 연결

// initialize global variables
alpha = _DIST_ALPHA;   // [2959] ema의 알파값 초기화
dist_ema = 0;          // [2959] dist_ema 초기화
former_reading = 0;
// move servo to neutral position
myservo.writeMicroseconds(_DUTY_NEU); // [2952] 서보 모터를 중간 위치에 지정

// initialize serial port
Serial.begin(57600); // [2952] 시리얼 포트를 57600의 속도로 연결

// convert angle speed into duty change per interval.
duty_chg_per_interval = (_DUTY_MAX - _DUTY_MIN) * (_SERVO_SPEED / 180.0) * (_INTERVAL_SERVO / 1000.0);                // [2959] 한 주기마다 이동할 양(180.0, 1000.0은 실수타입이기 때문에 나눗셈의 결과가 실수타입으로 리턴)
duty_chg_per_interval /=8;
// [2974] INTERVAL -> _INTERVAL_SERVO 로 수정

// [2974] 이벤트 변수 초기화
last_sampling_time_dist = 0; // [2974] 마지막 거리 측정 시간 초기화
last_sampling_time_servo = 0; // [2974] 마지막 서보 업데이트 시간 초기화
last_sampling_time_serial = 0; // [2974] 마지막 출력 시간 초기화
event_dist = event_servo = event_serial = false; // [2974] 각 이벤트 변수 false로 초기화
}
  

void loop() {
  float t = ir_distance();
/////////////////////
// Event generator //
/////////////////////
  unsigned long time_curr = millis();  // [2964] event 발생 조건 설정
  if(time_curr >= last_sampling_time_dist + _INTERVAL_DIST) {
    last_sampling_time_dist += _INTERVAL_DIST;
    event_dist = true; // [2957] 거리 측정 주기에 도달했다는 이벤트 발생
  }
  if(time_curr >= last_sampling_time_servo + _INTERVAL_SERVO) {
    last_sampling_time_servo += _INTERVAL_SERVO;
    event_servo = true; // [2957] 서보모터 제어 주기에 도달했다는 이벤트 발생
  }
  if(time_curr >= last_sampling_time_serial + _INTERVAL_SERIAL) {
    last_sampling_time_serial += _INTERVAL_SERIAL;
    event_serial = true; // [2957] 출력주기에 도달했다는 이벤트 발생
  }

////////////////////
// Event handlers //
////////////////////

  // get a distance reading from the distance sensor
  if(event_dist) { 
     event_dist = false;
      dist_raw = ir_distance();   // [2959] dist_raw에 필터링된 측정값 저장
      if (dist_ema == 0){                  // [2959] 맨 처음
        dist_ema = dist_raw;               // [2959] 맨 처음 ema값 = 필터링된 측정값
      }                                    // [2963] dist_ema를 dist_raw로 초기화
      else{
        dist_ema = alpha * dist_raw + (1-alpha) * dist_ema;   // [2959] ema 구현
      }  
//      if (dist_ema >= 245 && dist_ema <= 265) tau = 1600;
//      else if (dist_ema >= 235 && dist_ema <= 275)
//        if (dist_ema < 255) tau = 140*dist_ema - 32700; 
//        else tau = -140*dist_ema + 38700;// 200~1600
//      else tau = 200;


      if (dist_ema >= 250 && dist_ema <= 260) tau = 700;
      else if (dist_ema >= 245 && dist_ema <= 265) tau = 600;
      else if (dist_ema >= 240 && dist_ema <= 270) tau = 380;
      else if (dist_ema >= 235 && dist_ema <= 275) tau = 300;
      else tau = 200;

      
//      if (dist_ema >= 245 && dist_ema <= 265) {
//        tau = 1600;
//      }
//      else if (dist_ema >= 185 && dist_ema <= 325)
//        if (dist_ema < 255) tau = 25*dist_ema - 4525; 
//        else tau = -25*dist_ema + 8225;// 100~1600
//      else
//        tau = 100;
  // PID control logic
    error_curr = _DIST_TARGET - dist_ema;
    pterm = _KP * error_curr;
    dterm = _KD *(error_curr - error_prev);
    iterm += _KI * (error_curr);
    if(abs(iterm) > _ITERM_MAX) iterm=0;
//    if (iterm > _ITERM_MAX) iterm=_ITERM_MAX;
//    if (iterm < -_ITERM_MAX) iterm=-_ITERM_MAX; // - 주의
    control = pterm + dterm + iterm;

  // duty_target = f(duty_neutral, control)
    duty_target = _DUTY_NEU + control;
    if (duty_target < _DUTY_MIN) duty_target = _DUTY_MIN;
    if (duty_target > _DUTY_MAX) duty_target = _DUTY_MAX;
    error_prev = error_curr;
  // keep duty_target value within the range of [_DUTY_MIN, _DUTY_MAX]

  }
  
  if(event_servo) {
    event_servo = false; // [2974] 서보 이벤트 실행 후, 다음 주기를 기다리기 위해 이벤트 종료
    // adjust duty_curr toward duty_target by duty_chg_per_interval
    if(duty_target > duty_curr) {  // [2964] 현재 서보 각도 읽기
      duty_curr += duty_chg_per_interval; // [2961] duty_curr은 주기마다 duty_chg_per_interval만큼 증가
      if(duty_curr > duty_target) {duty_curr = duty_target;} // [2956] duty_target 지나쳤을 경우, duty_target에 도달한 것으로 duty_curr값 재설정
    }
    else {
      duty_curr -= duty_chg_per_interval;  // [2961] duty_curr은 주기마다 duty_chg_per_interval만큼 감소
      if (duty_curr < duty_target) {duty_curr = duty_target;} // [2956] duty_target 지나쳤을 경우, duty_target에 도달한 것으로 duty_curr값 재설정
    }
    // update servo position
    myservo.writeMicroseconds(duty_curr);  // [2964] 서보 움직임 조절

  }
  
  if(event_serial) {
    event_serial = false; // [2974] 출력 이벤트 실행 후, 다음 주기까지 이벤트 종료
//    Serial.print("Min:0,Low:200,dist:");
//    Serial.print(dist_raw); // [2957] 적외선 센서로부터 받은 값 출력
//    Serial.print(",pterm:");
//    Serial.print(pterm);
//    Serial.print(",duty_target:"); 
//    Serial.print(duty_target); // [2957] 목표로 하는 거리 출력
//    Serial.print(",duty_curr:"); 
//    Serial.print(duty_curr); // [2957] 서보모터에 입력한 값 출력
//    Serial.print(",dist_ema:"); 
//    Serial.print(dist_ema); //
//    Serial.println(",High:310,Max:2000");

    Serial.print("IR:");
    Serial.print(dist_ema);
    Serial.print(",T:");
    Serial.print(dist_target);
//    Serial.print(",ITERM:");
//    Serial.print(iterm+150);
    Serial.print(",P:");
    Serial.print(map(pterm,-1000,1000,510,610));
    Serial.print(",D:");
    Serial.print(map(dterm,-1000,1000,510,610));
    Serial.print(",I:");
    Serial.print(map(iterm,-1000,1000,510,610));
    Serial.print(",DTT:");
    Serial.print(map(duty_target,1000,2000,410,510));
    Serial.print(",DTC:");
    Serial.print(map(duty_curr,1000,2000,410,510));
    Serial.println(",-G:245,+G:265,m:0,M:800");
  }
}
float ir_distance(void){ // return value unit: mm
                         // [2959] 센서가 측정한 거리를 리턴해주는 함수
  float val, a, b, a_, b_;
  float volt = float(analogRead(PIN_IR));

  // for low pass filter
  double st_time = millis();
  double dt = st_time - pre_time;
  double af_val;
  
  val = ((6762.0/(volt-9.0))-4.0) * 10.0; // [2961] *10 : cm -> mm로 변환

  // spike filtering
  if (former > former_former*0.85 && val<former*0.91 && val<122) {
    val *= 1.15;
  }
  else if (former > former_former*0.85 && val<former*0.85 && val<200){
    val *= 1.2;
  }
  else if (former > former_former*0.85 && val<former*0.78 ){
    val *= 1.275;
  }
  former_former = former;
  former = val;
  // low pass filter
  af_val = tau/(tau+dt)*pre_val + dt/(tau+dt)*val;
  pre_val = af_val;
  pre_time = st_time;
  val = af_val;  
  // calibrate
  if (val < 73) return val *= 1.4;
  else if (val <= 132) {
    a = 77;
    b = 132;
    a_ = 105;
    b_ = 150.0;
  }
  else if (val <= 175){
    a = 132;
    b = 175;
    a_ = 150;
    b_ = 200.0;
  }
  else if (val <= 210){
    a = 175;
    b = 210;
    a_ = 200;
    b_ = 250.0;
  }
  else if (val <= 247){
    a = 210;
    b = 247;
    a_ = 250;
    b_ = 300.0;
  }
  else if (val <= 289) {
    a = 247;
    b = 289;
    a_ = 300;
    b_ = 350.0;
  }
  else if (val <= 337) {
    a = 289;
    b = 337;
    a_ = 350;
    b_ = 400.0;
  }
  else if (val <=394){
    a = 337;
    b = 394;
    a_ = 400;
    b_ = 445.0;
  }
  else return val *1.14;
  return a_ + (b_ - a_) / (b - a) * (val - a);  // [2964] 센서가 읽은 거리를 이용한 실제거리 반환
}                                              // [2959] 윗줄 ir_distance_filtered로 이동

//float ir_distance_filtered(void){ // return value unit: mm
//               // [2964] 적외선 센서를 이용한 거리 측정 함수
//             // [2959] 센서가 측정한 거리를 필터링한 값을 리턴
//                          // [2952] ir_distance()에서 리턴된 값을 val에 대입후 필터링 수행
//  val = ir_distance()
//  return 100 + 300.0 / (b - a) * (val - a); //[2959] 100mm일때 측정값과 400mm일때 측정값 기준으로 거리 보정
//
//}
