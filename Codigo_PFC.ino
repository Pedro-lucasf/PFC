/*
  Contador de piscadas + gravação em cartão SD - TCC
  Hardware: ESP32-C3 Supermini  +  MicroSD Card Adapter (modulo SPI de 6 pinos)

  MODOS (troque e faça novo upload)
  ---------------------------------
  MODO_PLOTTER = 1 -> Serial envia SOMENTE tensões (Serial Plotter)
  MODO_PLOTTER = 0 -> Serial envia SOMENTE a contagem (Serial Monitor)
  A gravação no SD funciona nos dois modos e independe do modo escolhido.

  LED AZUL EMBUTIDO (GPIO8) - útil quando a placa estiver longe do PC
  ---------------------------------------------------------------------
  - Pisca rápido sem parar  -> cartão SD não foi encontrado (contagem
    continua normalmente, só não é salva).
  - Um lampejo curto        -> uma piscada acabou de ser gravada no SD.
  - Apagado                 -> tudo funcionando, aguardando piscadas.

  ARQUIVOS GERADOS NO CARTÃO (novo conjunto a cada vez que a placa liga)
  -------------------------------------------------------------------
  /piscadas_001.csv  -> uma linha por piscada detectada
      tempo_total_ms;tempo_desde_ultima_ms;Esquerdo;Direito;Ambos;duracao_ms
      (Esquerdo/Direito/Ambos são contagens acumuladas até aquela linha)
  /resumo_001.txt    -> resumo legível, reescrito do zero a cada piscada
      (total por tipo, tempo total de coleta, frequência, menor/maior duração)
  /tensoes_001.csv   -> (opcional, LOG_TENSOES = 1) tensões a cada amostra
      tempo_ms;mv_esq;mv_dir

  - Separador ";" com valores inteiros: abre direto no Excel em português.
  - Os tempos contam a partir da ligação da placa (não há relógio real).

  LIGAÇÃO (ligação direta, sem ambiguidade de pinagem)
  ------------------------------------------------------
  Módulo microSD (rótulo)   Supermini
  VCC                       3V3
  GND                       GND
  SCK                       GPIO4
  MISO                      GPIO5
  MOSI                      GPIO6
  CS                        GPIO7
  Sensor esquerdo           GPIO0  (ADC)
  Sensor direito            GPIO1  (ADC)

  Prefira fios curtos e bem encaixados (ou soldados). Ver texto de
  resposta para detalhes sobre alimentação e cuidados de montagem.
*/


#include <SPI.h>
#include <SD.h>
#include <string.h>   // necessario para strncpy

// ===================== CONFIGURAÇÃO =====================
#define MODO_PLOTTER 1          // 1 = Serial Plotter | 0 = Serial Monitor
#define LOG_TENSOES  0          // 1 = grava também as tensões (arquivo maior)

const int PIN_ESQ = 0;          // GPIO0 - sensor esquerdo
const int PIN_DIR = 1;          // GPIO1 - sensor direito

// Pinos do cartão SD (SPI) - pinos padrão de SPI do ESP32-C3
const int PIN_SD_SCK  = 4;
const int PIN_SD_MISO = 5;
const int PIN_SD_MOSI = 6;
const int PIN_SD_CS   = 7;

const int PIN_LED = 8;          // LED azul embutido do Supermini (invertido)
const bool LED_INVERTIDO = true; // nesta placa, LOW = aceso

const float VREF    = 3.3;
const int   ADC_MAX = 4095;

const float LIM_BAIXO = 2.8;    // abaixo disso = olho fechado
const float LIM_ALTO  = 3.0;    // acima disso  = olho aberto

const unsigned long PERIODO_MS     = 10;
const unsigned long DURACAO_MIN_MS = 30;
const unsigned long DURACAO_MAX_MS = 2000;

const unsigned long PISCA_LED_ERRO_MS = 300; // período do pisca-pisca de erro
const unsigned long FLASH_LED_MS      = 60;  // duração do lampejo de gravação
// ========================================================

int Conta_Esq   = 0;
int Conta_Dir   = 0;
int Conta_Ambos = 0;

enum Estado { REPOUSO, PISCANDO };
Estado estado = REPOUSO;

bool esqFechou = false;
bool dirFechou = false;
unsigned long inicioPiscada = 0;
unsigned long ultimaAmostra = 0;

// ---- Estatísticas para o resumo ----
unsigned long ultimoFimPiscada = 0;          // 0 = ainda não houve piscada
unsigned long menorDuracao     = 0xFFFFFFFFUL;
unsigned long maiorDuracao     = 0;

// ---- SD ----
bool sdOk = false;
char nomeEventos[32];  // guarda o nome do piscadas_XXX.csv
char nomeResumo[32];   // guarda o nome do resumo_XXX.txt (mesmo índice do CSV)

#if LOG_TENSOES
File arqTensoes;
char bufTensoes[512];
size_t nBuf = 0;
unsigned long ultimoFlush = 0;
#endif

// ---- LED de status ----
unsigned long ledApagaEm    = 0;
unsigned long ultimoToggleErro = 0;
bool ledErroAceso = false;

void ledEscreve(bool aceso) {
  digitalWrite(PIN_LED, (aceso != LED_INVERTIDO) ? HIGH : LOW);
}

void ledFlash() {
  ledEscreve(true);
  ledApagaEm = millis() + FLASH_LED_MS;
}

void atualizarLed(unsigned long agora) {
  if (!sdOk) {
    // pisca continuamente para indicar erro
    if (agora - ultimoToggleErro >= PISCA_LED_ERRO_MS) {
      ultimoToggleErro = agora;
      ledErroAceso = !ledErroAceso;
      ledEscreve(ledErroAceso);
    }
    return;
  }
  // modo normal: apaga o lampejo de gravação quando o tempo expira
  if (ledApagaEm != 0 && agora >= ledApagaEm) {
    ledEscreve(false);
    ledApagaEm = 0;
  }
}

// Mensagens de texto só aparecem no modo Monitor (não poluem o Plotter)
void aviso(const char *msg) {
#if !MODO_PLOTTER
  Serial.println(msg);
#endif
}

void iniciarSD() {
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  SPI.setDataMode(SPI_MODE0);   // exigido pelo protocolo do cartao SD (ver doc oficial arduino-esp32)

  if (!SD.begin(PIN_SD_CS, SPI)) {
    aviso("ERRO: cartao SD nao encontrado. Confira ligacao, VCC/GND e formatacao FAT32.");
    return;
  }

  // Procura o primeiro número livre para não sobrescrever gravações antigas
  char nome[32];
  int indice = 1;
  for (; indice < 1000; indice++) {
    snprintf(nome, sizeof(nome), "/piscadas_%03d.csv", indice);
    if (!SD.exists(nome)) break;
  }

  strncpy(nomeEventos, nome, sizeof(nomeEventos));

  File fEventos = SD.open(nomeEventos, FILE_APPEND);
  if (!fEventos) {
    aviso("ERRO: nao foi possivel criar o arquivo de piscadas.");
    return;
  }
  fEventos.println("tempo_total_ms;tempo_desde_ultima_ms;Esquerdo;Direito;Ambos;duracao_ms");
  fEventos.flush();
  fEventos.close();   // fecha para o tamanho do cabecalho ficar gravado corretamente

  // mesmo indice do CSV, so que para o resumo em .txt
  snprintf(nomeResumo, sizeof(nomeResumo), "/resumo_%03d.txt", indice);

#if LOG_TENSOES
  snprintf(nome, sizeof(nome), "/tensoes_%03d.csv", indice);
  arqTensoes = SD.open(nome, FILE_APPEND);
  if (!arqTensoes) {
    aviso("ERRO: nao foi possivel criar o arquivo de tensoes.");
    return;
  }
  arqTensoes.println("tempo_ms;mv_esq;mv_dir");
  arqTensoes.flush();
#endif

  sdOk = true;
  aviso("SD OK: gravando dados no cartao.");
}

// Reescreve o resumo do zero (FILE_WRITE trunca o arquivo) a cada piscada.
// tempoTotalMs = instante atual (desde a ligacao da placa) usado como
// "tempo total de coleta" para calcular a frequencia de piscadas/min.
void atualizarResumo(unsigned long tempoTotalMs) {
  if (!sdOk) return;

  File f = SD.open(nomeResumo, FILE_WRITE);  // FILE_WRITE = sobrescreve do zero
  if (!f) return;

  int totalPiscadas = Conta_Esq + Conta_Dir + Conta_Ambos;
  float minutos    = tempoTotalMs / 60000.0;
  float frequencia = (minutos > 0) ? (totalPiscadas / minutos) : 0;

  f.println("=== Resumo da coleta ===");
  f.printf("Tempo total de coleta: %lu ms (%.2f min)\n\n", tempoTotalMs, minutos);

  f.printf("Piscadas - Esquerdo: %d\n", Conta_Esq);
  f.printf("Piscadas - Direito:  %d\n", Conta_Dir);
  f.printf("Piscadas - Ambos:    %d\n", Conta_Ambos);
  f.printf("Piscadas - Total:    %d\n\n", totalPiscadas);

  f.printf("Frequencia de piscadas: %.2f piscadas/min\n\n", frequencia);

  f.printf("Menor duracao de piscada: %lu ms\n", menorDuracao);
  f.printf("Maior duracao de piscada: %lu ms\n", maiorDuracao);

  f.close();   // aqui fechamos de fato, pois reabrimos este arquivo a cada piscada
}

#if LOG_TENSOES
void logTensao(unsigned long t, int mvEsq, int mvDir) {
  if (!sdOk) return;
  nBuf += snprintf(bufTensoes + nBuf, sizeof(bufTensoes) - nBuf,
                   "%lu;%d;%d\n", t, mvEsq, mvDir);
  if (nBuf > sizeof(bufTensoes) - 40 || (t - ultimoFlush) >= 1000) {
    arqTensoes.write((const uint8_t *)bufTensoes, nBuf);
    arqTensoes.flush();
    nBuf = 0;
    ultimoFlush = t;
  }
}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  ledEscreve(false);
  iniciarSD();
}

void registrarPiscada(bool esq, bool dir, unsigned long duracao, unsigned long agora) {
  const char *tipo;

  if (esq && dir) {
    Conta_Ambos++;
    tipo = "AMBOS";
  } else if (esq) {
    Conta_Esq++;
    tipo = "ESQUERDO";
  } else {
    Conta_Dir++;
    tipo = "DIREITO";
  }

  // tempo desde a piscada anterior (a primeira usa o tempo desde a ligacao)
  unsigned long tempoDesdeUltima = (ultimoFimPiscada == 0) ? agora : (agora - ultimoFimPiscada);

  if (duracao < menorDuracao) menorDuracao = duracao;
  if (duracao > maiorDuracao) maiorDuracao = duracao;

  if (sdOk) {
    File f = SD.open(nomeEventos, FILE_APPEND);
    if (f) {
      f.printf("%lu;%lu;%d;%d;%d;%lu\n",
               agora, tempoDesdeUltima,
               Conta_Esq, Conta_Dir, Conta_Ambos,
               duracao);
      f.flush();
      f.close();   // fecha para o tamanho/dado ficar gravado corretamente no cartao
    }
    ledFlash();
    atualizarResumo(agora);
  }

  ultimoFimPiscada = agora;

#if !MODO_PLOTTER
  Serial.print("[");
  Serial.print(millis() / 1000.0, 2);
  Serial.print(" s] Piscada ");
  Serial.print(tipo);
  Serial.print(" (");
  Serial.print(duracao);
  Serial.print(" ms)  |  Esq: ");
  Serial.print(Conta_Esq);
  Serial.print("  Dir: ");
  Serial.print(Conta_Dir);
  Serial.print("  Ambos: ");
  Serial.println(Conta_Ambos);
#endif
}

void loop() {
  unsigned long agora = millis();
  atualizarLed(agora);

  if (agora - ultimaAmostra < PERIODO_MS) return;
  ultimaAmostra = agora;

  float S_Esq = analogRead(PIN_ESQ) * (VREF / ADC_MAX);
  float S_Dir = analogRead(PIN_DIR) * (VREF / ADC_MAX);

  bool esqBaixo  = S_Esq < LIM_BAIXO;
  bool dirBaixo  = S_Dir < LIM_BAIXO;
  bool ambosAlto = (S_Esq > LIM_ALTO) && (S_Dir > LIM_ALTO);

  switch (estado) {
    case REPOUSO:
      if (esqBaixo || dirBaixo) {
        estado        = PISCANDO;
        inicioPiscada = agora;
        esqFechou     = esqBaixo;
        dirFechou     = dirBaixo;
      }
      break;

    case PISCANDO:
      if (esqBaixo) esqFechou = true;
      if (dirBaixo) dirFechou = true;

      if (ambosAlto) {
        unsigned long duracao = agora - inicioPiscada;
        if (duracao >= DURACAO_MIN_MS && duracao <= DURACAO_MAX_MS) {
          registrarPiscada(esqFechou, dirFechou, duracao, agora);
        }
        estado = REPOUSO;
      }
      break;
  }

#if LOG_TENSOES
  logTensao(agora, (int)(S_Esq * 1000), (int)(S_Dir * 1000));
#endif

#if MODO_PLOTTER
  Serial.print("Esquerdo:");   Serial.print(S_Esq, 3);
  Serial.print(",Direito:");   Serial.print(S_Dir, 3);
  Serial.print(",Lim_Baixo:"); Serial.print(LIM_BAIXO, 1);
  Serial.print(",Lim_Alto:");  Serial.println(LIM_ALTO, 1);
#else
  if (Serial.available() > 0 && Serial.read() == 'r') {
    Conta_Esq = Conta_Dir = Conta_Ambos = 0;
    ultimoFimPiscada = 0;
    menorDuracao = 0xFFFFFFFFUL;
    maiorDuracao = 0;
    Serial.println("Contadores zerados.");
  }
#endif
}
