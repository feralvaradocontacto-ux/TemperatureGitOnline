
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ============================================================
//                         CONFIGURACION
// ============================================================

// ---------------------- DHT11 -------------------------------

#define DHTPIN 2
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);


// ---------------------- LED RGB ------------------------------
// LED DE ANODO COMUN
// Rojo  = D3
// Verde = D5
// Azul  = D6

#define LED_R 3
#define LED_G 5
#define LED_B 6


// ---------------------- MOTOR --------------------------------
// PWM -> PC817 -> IRLZ44N
// Circuito con logica invertida

#define MOTOR_PIN 9


// ---------------------- OLED ---------------------------------

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

bool oledListo = false;


// ============================================================
//                    VARIABLES DE CONTROL
// ============================================================

// Guarda la velocidad anterior para detectar
// cuando el ventilador pasa de apagado a encendido.

int velocidadAnterior = 0;


// ============================================================
//                         LED RGB
// ============================================================

void setColor(int red, int green, int blue) {

  // LED RGB de anodo comun.
  // La salida PWM esta invertida.

  analogWrite(LED_R, 255 - red);
  analogWrite(LED_G, 255 - green);
  analogWrite(LED_B, 255 - blue);
}


// ============================================================
//                       PANTALLA OLED
// ============================================================

void actualizarOLED(float temperatura, int velocidad,
                    bool errorSensor = false) {

  if (!oledListo) {
    return;
  }

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  if (errorSensor) {

    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("ERROR SENSOR");

    display.setCursor(0, 16);
    display.print("FAN: 100%");
  }

  else {

    // Encabezado

    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("TEMPERATURA:");

    // Temperatura en caracteres grandes

    display.setTextSize(2);

    display.setCursor(0, 12);
    display.print(temperatura, 1);
    display.print(" C");

    // Velocidad del ventilador

    display.setTextSize(1);

    display.setCursor(78, 20);
    display.print("FAN:");
    display.print(velocidad);
    display.print("%");
  }

  display.display();
}


// ============================================================
//                    SETUP
// ============================================================

void setup() {

  Serial.begin(9600);

  // Inicializar sensor

  dht.begin();

  // Configurar pines RGB

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  // Configurar motor

  pinMode(MOTOR_PIN, OUTPUT);

  // Estado inicial:
  // Motor apagado (D9 HIGH por inversion)
  // LED azul encendido

  analogWrite(MOTOR_PIN, 255);

  setColor(0, 0, 255);


  // Inicializar comunicacion I2C

  Wire.begin();

  // Inicializar OLED

  oledListo = display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDR
  );

  if (oledListo) {

    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print("ECU VENTILACION");

    display.setCursor(0, 16);
    display.print("Sistema listo");

    display.display();
  }

  else {

    Serial.println("ERROR: OLED NO INICIALIZADO");
  }

  Serial.println();
  Serial.println("==============================");
  Serial.println("      ECU VENTILACION");
  Serial.println("==============================");
  Serial.println("Sistema iniciado");
  Serial.println("DHT11: D2");
  Serial.println("LED RGB: D3, D5, D6");
  Serial.println("PC817: D9");
  Serial.println("OLED: I2C A4/A5");
  Serial.println("==============================");

  delay(1500);
}


// ============================================================
//                          LOOP
// ============================================================

void loop() {

  // DHT11 necesita tiempo entre lecturas

  delay(2000);

  float temperatura = dht.readTemperature();


  // ==========================================================
  //                         FAIL-SAFE
  // ==========================================================

  if (isnan(temperatura)) {

    // Motor al 100 %
    // PC817 invertido: D9 LOW

    analogWrite(MOTOR_PIN, 0);

    // LED rojo

    setColor(255, 0, 0);

    // OLED

    actualizarOLED(0, 100, true);

    // Actualizar estado anterior

    velocidadAnterior = 100;

    // Monitor serial

    Serial.println();
    Serial.println("==============================");
    Serial.println("       FALLA DE SENSOR");
    Serial.println("==============================");
    Serial.println("DHT11 sin respuesta");
    Serial.println("FAIL-SAFE ACTIVADO");
    Serial.println("VENTILADOR: 100%");
    Serial.println("==============================");

    return;
  }


  // ==========================================================
  //                    VARIABLES DE CONTROL
  // ==========================================================

  int pwmMotor = 0;
  int pwmSalida = 0;
  int velocidad = 0;

  const char* estado = "";


  // ==========================================================
  //                       ZONA AZUL
  //                         0 - 15 C
  // ==========================================================

  if (temperatura < 16.0) {

    estado = "AZUL - APAGADO";

    // LED azul

    setColor(0, 0, 255);

    // Motor apagado

    velocidad = 0;
    pwmMotor = 0;
  }


  // ==========================================================
  //                       ZONA VERDE
  //                        16 - 30 C
  // ==========================================================

  else if (temperatura < 31.0) {

    estado = "VERDE";

    // LED verde exclusivamente

    setColor(0, 255, 0);


    // --------------------------------------------------------
    // INTERPOLACION LINEAL
    //
    // 16 C -> 1 %
    // 30 C -> 80 %
    // --------------------------------------------------------

    float porcentaje = 1.0 +
      ((temperatura - 16.0) * 79.0 / 14.0);

    // Redondear porcentaje

    velocidad = (int)round(porcentaje);

    velocidad = constrain(velocidad, 1, 80);


    // Convertir porcentaje a PWM

    pwmMotor = (int)round(
      velocidad * 255.0 / 100.0
    );
  }


  // ==========================================================
  //                        ZONA ROJA
  //                       31 C EN ADELANTE
  // ==========================================================

  else {

    estado = "ROJO - MAXIMO";

    // LED rojo exclusivamente

    setColor(255, 0, 0);

    // Ventilador al 100 %

    velocidad = 100;
    pwmMotor = 255;
  }


  // ==========================================================
  //                  INVERSION PARA PC817
  // ==========================================================

  // Nuestro circuito tiene logica invertida:
  //
  // 0 % motor   -> D9 = 255
  // 100 % motor -> D9 = 0
  //
  // Por lo tanto:

  pwmSalida = 255 - pwmMotor;


  // ==========================================================
  //                       KICK-START
  // ==========================================================

  // Si el ventilador estaba apagado y ahora
  // debe comenzar a funcionar, aplicar impulso inicial.

  if (velocidadAnterior == 0 && velocidad > 0) {

    // Impulso de arranque al 100 %

    analogWrite(MOTOR_PIN, 0);

    delay(250);
  }

  // Aplicar PWM correspondiente a la temperatura

  analogWrite(MOTOR_PIN, pwmSalida);


  // Registrar velocidad anterior

  velocidadAnterior = velocidad;


  // ==========================================================
  //                         OLED
  // ==========================================================

  actualizarOLED(temperatura, velocidad);


  // ==========================================================
  //                    MONITOR SERIAL
  // ==========================================================

  Serial.println();

  Serial.println("==============================");
  Serial.println("       ECU VENTILACION");
  Serial.println("==============================");

  Serial.print("Temperatura : ");
  Serial.print(temperatura, 1);
  Serial.println(" C");

  Serial.print("Estado LED  : ");
  Serial.println(estado);

  Serial.print("Ventilador  : ");
  Serial.print(velocidad);
  Serial.println("%");

  Serial.print("PWM motor   : ");
  Serial.println(pwmMotor);

  Serial.print("PWM D9      : ");
  Serial.println(pwmSalida);

  Serial.println("==============================");
}
