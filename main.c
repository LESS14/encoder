#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "std_image.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum {
    SILENCE,
    TONE,
    LINEAR_SWEEP
} AudioSymbolType;

typedef struct {
    double duration;
    AudioSymbolType type;
} AudioSymbol;

typedef struct {
    AudioSymbol base;
} Silence;

typedef struct {
    AudioSymbol base;
    double frequency;
    double amplitude;
    double phase;
} Tone;

typedef struct {
    AudioSymbol base;
    double freqstart;
    double freqend;
    double amplitude;
    double phase;
} LinearSweep;

int16_t* generate_wav(AudioSymbol** syms, int sym_count, int samplerate, int* wav_length) {
    int total_samples = 0;
    for (int i = 0; i < sym_count; ++i) {
        total_samples += (int)(syms[i]->duration * samplerate);
    }

    int16_t* wav = (int16_t*)malloc(total_samples * sizeof(int16_t));
    int idx = 0;
    double last_theta = 0.0;

    for (int i = 0; i < sym_count; ++i) {
        int sample_count = (int)(syms[i]->duration * samplerate);
        switch (syms[i]->type) {
        case SILENCE:
            memset(&wav[idx], 0, sample_count * sizeof(int16_t));
            idx += sample_count;
            break;
        case TONE: {
            Tone* tone = (Tone*)syms[i];
            double rfreq = 2 * M_PI * tone->frequency / samplerate;
            for (int j = 0; j < sample_count; ++j) {
                wav[idx++] = (int16_t)(tone->amplitude * sin(j * rfreq + tone->phase) * 32767);
            }
            last_theta = sample_count * rfreq + tone->phase;
            break;
        }
        case LINEAR_SWEEP: {
            LinearSweep* sweep = (LinearSweep*)syms[i];
            double phase = sweep->phase;
            double freq_diff = sweep->freqend - sweep->freqstart;
            for (int j = 0; j < sample_count; ++j) {
                double freq = sweep->freqstart + (freq_diff * j / sample_count);
                double rfreq = 2 * M_PI * freq / samplerate;
                wav[idx++] = (int16_t)(sweep->amplitude * sin(phase) * 32767);
                phase += rfreq;
            }
            last_theta = phase;
            break;
        }
        }
    }

    *wav_length = total_samples;
    return wav;
}

AudioSymbol** generate_vox_signal(int* sym_count) {
    int freqs[] = { 1900, 1500, 1900, 1500, 2300, 1500, 2300, 1500 };
    *sym_count = sizeof(freqs) / sizeof(freqs[0]);
    AudioSymbol** syms = (AudioSymbol**)malloc(*sym_count * sizeof(AudioSymbol*));

    for (int i = 0; i < *sym_count; ++i) {
        Tone* tone = (Tone*)malloc(sizeof(Tone));
        tone->base.duration = 0.1;
        tone->base.type = TONE;
        tone->frequency = freqs[i];
        tone->amplitude = 1.0;
        tone->phase = 0;
        syms[i] = (AudioSymbol*)tone;
    }
    return syms;
}

AudioSymbol** generate_vis_signal(int id, int* sym_count) {
    int freqs[] = { 1900, 1200, 1900, 1200, 1100, 1300, 1100, 1300, 1100, 1300, 1100, 1300, 1100, 1300, 1200 };
    *sym_count = sizeof(freqs) / sizeof(freqs[0]);
    AudioSymbol** syms = (AudioSymbol**)malloc(*sym_count * sizeof(AudioSymbol*));

    for (int i = 0; i < *sym_count; ++i) {
        Tone* tone = (Tone*)malloc(sizeof(Tone));
        tone->base.duration = (i == 1 || i == *sym_count - 1) ? 0.01 : 0.03;
        tone->base.type = TONE;
        tone->frequency = freqs[i];
        tone->amplitude = 1.0;
        tone->phase = 0;
        syms[i] = (AudioSymbol*)tone;
    }
    return syms;
}

AudioSymbol** generate_eof_signal(int* sym_count) {
    int freqs[] = { 1900, 1500, 1900, 1500 };
    *sym_count = sizeof(freqs) / sizeof(freqs[0]);
    AudioSymbol** syms = (AudioSymbol**)malloc(*sym_count * sizeof(AudioSymbol*));

    for (int i = 0; i < *sym_count; ++i) {
        Tone* tone = (Tone*)malloc(sizeof(Tone));
        tone->base.duration = 0.1;
        tone->base.type = TONE;
        tone->frequency = freqs[i];
        tone->amplitude = 1.0;
        tone->phase = 0;
        syms[i] = (AudioSymbol*)tone;
    }
    return syms;
}

AudioSymbol** convert_flag_row(uint8_t* row, int row_length, double duration, int samplerate, int* sym_count) {
    double padding = 0.13974 - duration;
    *sym_count = row_length + 2;
    AudioSymbol** syms = (AudioSymbol**)malloc(*sym_count * sizeof(AudioSymbol*));
    int idx = 0;

    Tone* tone = (Tone*)malloc(sizeof(Tone));
    tone->base.duration = padding / 2.0;
    tone->base.type = TONE;
    tone->frequency = 2300;
    tone->amplitude = 1.0;
    tone->phase = 0;
    syms[idx++] = (AudioSymbol*)tone;

    for (int i = 0; i < row_length; ++i) {
        tone = (Tone*)malloc(sizeof(Tone));
        tone->base.duration = 1.0 / samplerate;
        tone->base.type = TONE;
        tone->frequency = 1550 + 700 * row[i] / 255.0;
        tone->amplitude = 1.0;
        tone->phase = 0;
        syms[idx++] = (AudioSymbol*)tone;
    }

    tone = (Tone*)malloc(sizeof(Tone));
    tone->base.duration = padding / 2.0;
    tone->base.type = TONE;
    tone->frequency = 2300;
    tone->amplitude = 1.0;
    tone->phase = 0;
    syms[idx++] = (AudioSymbol*)tone;

    return syms;
}


AudioSymbol** generate_rows(int* row_count, int samplerate) {
    int width, height, channels;
    uint8_t* cover_img = stbi_load("cover.png", &width, &height, &channels, 0);
    uint8_t* flag_img = stbi_load("flag.png", &width, &height, &channels, 0);

    if (!cover_img) {
        fprintf(stderr, "Error: Unable to load cover.png\n");
        exit(1);
    }

    if (!flag_img) {
        fprintf(stderr, "Error: Unable to load flag.png\n");
        exit(1);
    }

    assert(width == 320 && height == 256);

    int flag_x = 258, flag_y = 44, flag_w = 16, flag_h = 16;

    double colordur = 0.13974;
    *row_count = height * 3;
    AudioSymbol** syms = (AudioSymbol**)malloc(*row_count * width * sizeof(AudioSymbol*));
    int idx = 0;

    for (int y = 0; y < height; ++y) {
        for (int chan = 0; chan < 3; ++chan) {
            if (chan == 2) {
                Tone* hsync = (Tone*)malloc(sizeof(Tone));
                hsync->base.duration = 0.009;
                hsync->base.type = TONE;
                hsync->frequency = 1200;
                hsync->amplitude = 1.0;
                hsync->phase = 0;
                syms[idx++] = (AudioSymbol*)hsync;
            }

            Tone* tone = (Tone*)malloc(sizeof(Tone));
            tone->base.duration = colordur / width / 2;
            tone->base.type = TONE;
            tone->frequency = 1500;
            tone->amplitude = 1.0;
            tone->phase = 0;
            syms[idx++] = (AudioSymbol*)tone;

            for (int x = 0; x < width - 1; ++x) {
                LinearSweep* sweep = (LinearSweep*)malloc(sizeof(LinearSweep));
                sweep->base.duration = colordur / width;
                sweep->base.type = LINEAR_SWEEP;
                sweep->freqstart = 1500 + 800 * cover_img[(y * width + x) * channels + chan] / 255.0;
                sweep->freqend = 1500 + 800 * cover_img[(y * width + x + 1) * channels + chan] / 255.0;
                sweep->amplitude = 1.0;
                sweep->phase = 0;
                syms[idx++] = (AudioSymbol*)sweep;
            }

            tone = (Tone*)malloc(sizeof(Tone));
            tone->base.duration = colordur / width / 2;
            tone->base.type = TONE;
            tone->frequency = 1500;
            tone->amplitude = 1.0;
            tone->phase = 0;
            syms[idx++] = (AudioSymbol*)tone;

            if (flag_y <= y && y < flag_y + flag_h) {
                int flag_row_length = flag_w;
                int flag_sym_count;
                AudioSymbol** flag_syms = convert_flag_row(&flag_img[(y - flag_y) * width + flag_x], flag_row_length, colordur * flag_w / width, samplerate, &flag_sym_count);
                for (int i = 0; i < flag_sym_count; ++i) {
                    syms[idx++] = flag_syms[i];
                }
                free(flag_syms);
            }
        }
    }

    stbi_image_free(cover_img);
    stbi_image_free(flag_img);

    return syms;
}

void save_wav_file(const char* filename, int samplerate, int16_t* data, int length) {
    FILE* file = fopen(filename, "wb");

    if (!file) {
        fprintf(stderr, "Error: Unable to open file %s\n", filename);
        return;
    }

    int32_t chunk_size = 36 + length * sizeof(int16_t);
    int16_t audio_format = 1;
    int16_t num_channels = 1;
    int32_t byte_rate = samplerate * num_channels * sizeof(int16_t);
    int16_t block_align = num_channels * sizeof(int16_t);
    int16_t bits_per_sample = sizeof(int16_t) * 8;

    fwrite("RIFF", 1, 4, file);
    fwrite(&chunk_size, sizeof(chunk_size), 1, file);
    fwrite("WAVE", 1, 4, file);

    fwrite("fmt ", 1, 4, file);
    int32_t subchunk1_size = 16;
    fwrite(&subchunk1_size, sizeof(subchunk1_size), 1, file);
    fwrite(&audio_format, sizeof(audio_format), 1, file);
    fwrite(&num_channels, sizeof(num_channels), 1, file);
    fwrite(&samplerate, sizeof(samplerate), 1, file);
    fwrite(&byte_rate, sizeof(byte_rate), 1, file);
    fwrite(&block_align, sizeof(block_align), 1, file);
    fwrite(&bits_per_sample, sizeof(bits_per_sample), 1, file);

    fwrite("data", 1, 4, file);
    int32_t subchunk2_size = length * sizeof(int16_t);
    fwrite(&subchunk2_size, sizeof(subchunk2_size), 1, file);
    fwrite(data, sizeof(int16_t), length, file);

    fclose(file);
}

int main() {
    int samplerate = 96000;
    int sym_count;

    AudioSymbol* silence = (AudioSymbol*)malloc(sizeof(Silence));
    silence->duration = 0.1;
    silence->type = SILENCE;

    AudioSymbol** vox_syms = generate_vox_signal(&sym_count);
    AudioSymbol** vis_syms = generate_vis_signal(60, &sym_count);
    AudioSymbol** eof_syms = generate_eof_signal(&sym_count);

    int row_count;
    AudioSymbol** rows = generate_rows(&row_count, samplerate);

    AudioSymbol** sstv = (AudioSymbol**)malloc((1 + sym_count + sym_count + 1 + row_count + sym_count + 1) * sizeof(AudioSymbol*));
    int idx = 0;
    sstv[idx++] = silence;

    for (int i = 0; i < sym_count; ++i) {
        sstv[idx++] = vox_syms[i];
    }
    for (int i = 0; i < sym_count; ++i) {
        sstv[idx++] = vis_syms[i];
    }

    AudioSymbol* hsync = (AudioSymbol*)malloc(sizeof(Tone));
    hsync->duration = 0.009;
    hsync->type = TONE;
    sstv[idx++] = hsync;

    for (int i = 0; i < row_count; ++i) {
        sstv[idx++] = rows[i];
    }

    for (int i = 0; i < sym_count; ++i) {
        sstv[idx++] = eof_syms[i];
    }
    sstv[idx++] = silence;

    int wav_length;
    int16_t* wav_data = generate_wav(sstv, idx, samplerate, &wav_length);

    save_wav_file("output.wav", samplerate, wav_data, wav_length);

    free(wav_data);
    free(sstv);
    free(vox_syms);
    free(vis_syms);
    free(eof_syms);
    free(rows);
    free(silence);
    free(hsync);

    return 0;
}
