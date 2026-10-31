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
