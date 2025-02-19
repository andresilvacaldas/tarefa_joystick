#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/bootrom.h"

/* Definições de pinos e parâmetros gerais */
#define I2C_INTERFACE  i2c1
#define SDA_PIN        14
#define SCL_PIN        15
#define DISPLAY_ADDR   0x3C
#define ANALOG_X       26 
#define ANALOG_Y       27  
#define BTN_JOYSTICK   22 
#define BTN_AUX        5 
#define LED_R          13
#define LED_B          12
#define LED_G          11
#define PWM_RATE       5000

/* Variáveis de controle de estado */
static volatile uint32_t lastPressTime = 0;
bool pwmActive = true;
bool borderVisible = false;

/* Configura o PWM para um LED em um pino específico */
void setupPWM(uint pin, uint freq) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, 255);
    pwm_set_clkdiv(slice, (float)48000000 / freq / 256);
    pwm_set_enabled(slice, true);
}

/* Callback para capturar eventos dos botões e tratar debounce */
void handleButtonPress(uint gpio, uint32_t events) {
    printf("Botão acionado: GPIO %d\n", gpio);
    uint32_t currentTime = to_us_since_boot(get_absolute_time());

    if (currentTime - lastPressTime > 300000) {  /* Prevenção contra acionamentos repetidos */
        lastPressTime = currentTime;

        if (gpio == BTN_JOYSTICK) {
            gpio_put(LED_G, !gpio_get(LED_G));
            borderVisible = !borderVisible;
        } else if (gpio == BTN_AUX) {
            pwmActive = !pwmActive;
        }

        /* Se o PWM for desativado, apaga os LEDs controlados por ele */
        if (!pwmActive) {
            pwm_set_gpio_level(LED_R, 0);
            pwm_set_gpio_level(LED_B, 0);
        }
    }
}

/* Converte o valor lido do ADC para um nível PWM de 0 a 255 */
uint8_t adcToPWM(uint16_t adcValue) {
    return (uint8_t)((abs(2048 - adcValue) / 2048.0) * 255);
}

/* Configuração inicial para os botões físicos */
void setupButton(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
    gpio_set_irq_enabled_with_callback(pin, GPIO_IRQ_EDGE_FALL, true, &handleButtonPress);
}

/* Inicializa a comunicação I2C e configura os pinos necessários */
void initializeI2C(void) {
    i2c_init(I2C_INTERFACE, 400 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
}

/* Prepara o display OLED para uso */
void configureDisplay(ssd1306_t *display) {
    ssd1306_init(display, WIDTH, HEIGHT, false, DISPLAY_ADDR, I2C_INTERFACE);
    ssd1306_config(display);
    ssd1306_send_data(display);
    ssd1306_fill(display, false);
    ssd1306_send_data(display);
}

/* Inicializa o ADC e os canais do joystick */
void setupADC(void) {
    adc_init();
    adc_gpio_init(ANALOG_X);
    adc_gpio_init(ANALOG_Y);
}

/* Obtém os valores do ADC para os eixos X e Y do joystick */
void readJoystick(uint16_t *adcX, uint16_t *adcY) {
    adc_select_input(0);
    *adcX = adc_read();
    adc_select_input(1);
    *adcY = adc_read();
}

/* Ajusta a posição do cursor na tela com base no joystick */
void updateCursorPosition(uint16_t adcX, uint16_t adcY, float *xPos, float *yPos) {
    float tempX = 64 - ((float)adcX / 4095.0f) * 64;
    float tempY = ((float)adcY / 4095.0f) * 128;

    *xPos = (tempX < 0) ? 0 : (tempX > 56 ? 56 : tempX);
    *yPos = (tempY < 0) ? 0 : (tempY > 120 ? 120 : tempY);
}

/* Atualiza os LEDs RGB conforme os valores do joystick */
void updateLEDs(uint16_t adcX, uint16_t adcY) {
    if (pwmActive) {
        uint8_t pwmR = adcToPWM(adcY);
        uint8_t pwmB = adcToPWM(adcX);
        pwm_set_gpio_level(LED_R, pwmR);
        pwm_set_gpio_level(LED_B, pwmB);

        if (adcX > 2000 && adcX < 2100) {
            pwm_set_gpio_level(LED_R, 0);
        }
        if (adcY > 2000 && adcY < 2100) {
            pwm_set_gpio_level(LED_B, 0);
        }
    } else {
        pwm_set_gpio_level(LED_R, 0);
        pwm_set_gpio_level(LED_B, 0);
    }
}

/* Atualiza a interface gráfica no OLED */
void drawOnDisplay(ssd1306_t *display, float xPos, float yPos, bool colorMode) {
    ssd1306_fill(display, !colorMode);

    if (borderVisible) {
        ssd1306_rect(display, 0, 0, 128, 64, colorMode, !colorMode);
    } else {
        ssd1306_rect(display, 0, 0, 128, 64, !colorMode, colorMode);
    }
    ssd1306_rect(display, (int)xPos, (int)yPos, 8, 8, colorMode, colorMode);
    ssd1306_send_data(display);
}

int main(void) {
    float cursorX = 0.0f, cursorY = 0.0f;
    uint16_t adcX = 0, adcY = 0;
    bool displayColor = true;
    ssd1306_t screen;

    setupPWM(LED_R, PWM_RATE);
    setupPWM(LED_B, PWM_RATE);

    gpio_init(LED_G);
    gpio_set_dir(LED_G, GPIO_OUT);

    setupButton(BTN_JOYSTICK);
    setupButton(BTN_AUX);

    initializeI2C();
    configureDisplay(&screen);

    setupADC();

    while (true) {
        readJoystick(&adcX, &adcY);
        updateCursorPosition(adcX, adcY, &cursorX, &cursorY);
        updateLEDs(adcX, adcY);
        drawOnDisplay(&screen, cursorX, cursorY, displayColor);
        sleep_ms(10);
    }

    return 0;
}
