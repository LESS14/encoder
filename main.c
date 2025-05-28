#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLERATE 96000
#define OUTPUT_FILENAME "output_aprimorado.wav"
#define COVER_IMG_FILENAME "input1.png"
#define FLAG_IMG_FILENAME "input2.png"

#define COVER_IMG_WIDTH 320
#define COVER_IMG_HEIGHT 256

#define FLAG_IMG_WIDTH 16 
#define FLAG_IMG_HEIGHT 16
#define FLAG_IMG_POS_Y 44 
#define SSTV_COLOR_SCANLINE_DURATION 0.13974
#define SSTV_FLAG_SEGMENT_TOTAL_DURATION 0.0464

#define SSTV_HSYNC_FREQ 1200.0
#define SSTV_HSYNC_DURATION 0.009

#define SSTV_PORCH_FREQ 1500.0
#define SSTV_PIXEL_FREQ_MIN 1500.0
#define SSTV_PIXEL_FREQ_MAX 2300.0
#define SSTV_PIXEL_FREQ_RANGE (SSTV_PIXEL_FREQ_MAX - SSTV_PIXEL_FREQ_MIN)

#define SSTV_FLAG_PAD_SYNC_FREQ 2300.0
#define SSTV_FLAG_PIXEL_FREQ_MIN 1550.0
#define SSTV_FLAG_PIXEL_FREQ_MAX 2250.0
#define SSTV_FLAG_PIXEL_FREQ_RANGE (SSTV_FLAG_PIXEL_FREQ_MAX - SSTV_FLAG_PIXEL_FREQ_MIN)

#define SSTV_VOX_TONE_DURATION 0.1
#define SSTV_VIS_HEADER_TONE_DURATION_SHORT 0.01
#define SSTV_VIS_HEADER_TONE_DURATION_LONG 0.03 
#define SSTV_EOF_TONE_DURATION 0.1
#define SSTV_SILENCE_DURATION 0.1

typedef enum {
    SILENCE_SYMBOL,
    TONE_SYMBOL,
    LINEAR_SWEEP_SYMBOL
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
    double phase_offset;
} Tone;

typedef struct {
    AudioSymbol base;
    double freqstart;
    double freqend;
    double amplitude;
    double phase_offset;
} LinearSweep;

void free_audio_symbols_array(AudioSymbol** syms, int sym_count);
AudioSymbol* create_silence_symbol(double duration);
AudioSymbol* create_tone_symbol(double duration, double freq, double amp, double phase_offset_rad);
AudioSymbol* create_sweep_symbol(double duration, double f_start, double f_end, double amp, double phase_offset_rad);


int16_t* generate_wav(AudioSymbol** syms, int sym_count, int samplerate_local, int* wav_length) {
    long long total_samples_long = 0;
    for (int i = 0; i < sym_count; ++i) {
        if (syms[i]) {
            total_samples_long += (long long)(syms[i]->duration * samplerate_local);
        }
    }

    if (total_samples_long == 0) {
        fprintf(stderr, "ERRO: Nenhum símbolo para gerar áudio ou duração total zero.\n");
        *wav_length = 0;
        return NULL;
    }
    if (total_samples_long > INT32_MAX) {
        fprintf(stderr, "ERRO: Número total de amostras excede o limite de int32_t!\n");
        *wav_length = 0;
        return NULL;
    }
    int total_samples = (int)total_samples_long;

    int16_t* wav = (int16_t*)malloc(total_samples * sizeof(int16_t));
    if (!wav) {
        perror("malloc para buffer WAV falhou");
        *wav_length = 0;
        return NULL;
    }

    int current_sample_idx = 0;
    double current_main_phase = 0.0;

    for (int i = 0; i < sym_count; ++i) {
        if (!syms[i]) continue;
        int sample_count_for_symbol = (int)(syms[i]->duration * samplerate_local);
        if (current_sample_idx + sample_count_for_symbol > total_samples) {
             sample_count_for_symbol = total_samples - current_sample_idx;
             if (sample_count_for_symbol <= 0 && i < sym_count -1) {
                fprintf(stderr, "Aviso: Buffer WAV cheio antes do símbolo %d. Símbolos restantes ignorados.\n", i);
                break; 
             }
        }
        if (sample_count_for_symbol < 0) sample_count_for_symbol = 0;


        switch (syms[i]->type) {
        case SILENCE_SYMBOL:
            if (sample_count_for_symbol > 0) {
                memset(&wav[current_sample_idx], 0, sample_count_for_symbol * sizeof(int16_t));
            }
            break;
        case TONE_SYMBOL: {
            Tone* tone = (Tone*)syms[i];
            double rfreq = 2.0 * M_PI * tone->frequency / samplerate_local;
            double symbol_start_phase = fmod(current_main_phase + tone->phase_offset, 2.0 * M_PI);
            for (int j = 0; j < sample_count_for_symbol; ++j) {
                wav[current_sample_idx + j] = (int16_t)(tone->amplitude * sin(j * rfreq + symbol_start_phase) * 32767.0);
            }
            if (sample_count_for_symbol > 0) {
                 current_main_phase = fmod(symbol_start_phase + sample_count_for_symbol * rfreq, 2.0 * M_PI);
            }
            break;
        }
        case LINEAR_SWEEP_SYMBOL: {
            LinearSweep* sweep = (LinearSweep*)syms[i];
            double phase_for_sweep = fmod(current_main_phase + sweep->phase_offset, 2.0 * M_PI);
            double freq_diff = sweep->freqend - sweep->freqstart;

            for (int j = 0; j < sample_count_for_symbol; ++j) {
                double instantaneous_freq = sweep->freqstart + (freq_diff * (double)j / sample_count_for_symbol);
                double rfreq_step = 2.0 * M_PI * instantaneous_freq / samplerate_local;
                wav[current_sample_idx + j] = (int16_t)(sweep->amplitude * sin(phase_for_sweep) * 32767.0);
                phase_for_sweep = fmod(phase_for_sweep + rfreq_step, 2.0 * M_PI);
            }
            if (sample_count_for_symbol > 0) {
                current_main_phase = phase_for_sweep;
            }
            break;
        }
        default:
             if (sample_count_for_symbol > 0) {
                memset(&wav[current_sample_idx], 0, sample_count_for_symbol * sizeof(int16_t));
            }
            break;
        }
        current_sample_idx += sample_count_for_symbol;
    }

    *wav_length = current_sample_idx;
    return wav;
}


AudioSymbol** generate_vox_signal(int* sym_count_out) {
    int freqs[] = { 1900, 1500, 1900, 1500, 2300, 1500, 2300, 1500 };
    *sym_count_out = sizeof(freqs) / sizeof(freqs[0]);
    AudioSymbol** syms = (AudioSymbol**)malloc(*sym_count_out * sizeof(AudioSymbol*));
    if (!syms) { *sym_count_out = 0; perror("malloc vox_syms"); return NULL; }

    for (int i = 0; i < *sym_count_out; ++i) {
        syms[i] = create_tone_symbol(SSTV_VOX_TONE_DURATION, freqs[i], 1.0, 0.0);
        if (!syms[i]) { free_audio_symbols_array(syms, i); *sym_count_out = 0; return NULL; }
    }
    return syms;
}

AudioSymbol** generate_vis_signal(int* sym_count_out) {
    int freqs[] = {
        1900, 1200, 1900, 1200, 1100, 1300, 1100, 1300, 1100, 1300, 1100, 1300, 1100, 1300, 1200
    };
    *sym_count_out = sizeof(freqs) / sizeof(freqs[0]);
    AudioSymbol** syms = (AudioSymbol**)malloc(*sym_count_out * sizeof(AudioSymbol*));
    if (!syms) { *sym_count_out = 0; perror("malloc vis_syms"); return NULL; }

    for (int i = 0; i < *sym_count_out; ++i) {
        double duration = (i == 1 || i == *sym_count_out - 1) ?
                           SSTV_VIS_HEADER_TONE_DURATION_SHORT : SSTV_VIS_HEADER_TONE_DURATION_LONG;
        syms[i] = create_tone_symbol(duration, freqs[i], 1.0, 0.0);
        if (!syms[i]) { free_audio_symbols_array(syms, i); *sym_count_out = 0; return NULL; }
    }
    return syms;
}

AudioSymbol** generate_eof_signal(int* sym_count_out) {
    int freqs[] = { 1900, 1500, 1900, 1500 };
    *sym_count_out = sizeof(freqs) / sizeof(freqs[0]);
    AudioSymbol** syms = (AudioSymbol**)malloc(*sym_count_out * sizeof(AudioSymbol*));
    if (!syms) { *sym_count_out = 0; perror("malloc eof_syms"); return NULL; }

    for (int i = 0; i < *sym_count_out; ++i) {
        syms[i] = create_tone_symbol(SSTV_EOF_TONE_DURATION, freqs[i], 1.0, 0.0);
        if (!syms[i]) { free_audio_symbols_array(syms, i); *sym_count_out = 0; return NULL; }
    }
    return syms;
}

AudioSymbol** convert_flag_row_to_symbols(
    uint8_t* flag_pixel_row_data,
    int num_flag_pixels_in_row,
    double total_time_for_this_flag_segment,
    int* generated_sym_count
) {
    if (num_flag_pixels_in_row <= 0) {
        fprintf(stderr, "ERRO: num_flag_pixels_in_row deve ser positivo em convert_flag_row_to_symbols.\n");
        *generated_sym_count = 0;
        return NULL;
    }

    *generated_sym_count = num_flag_pixels_in_row + 2; // +2 para tons de padding/sync
    AudioSymbol** syms = (AudioSymbol**)malloc(*generated_sym_count * sizeof(AudioSymbol*));
    if (!syms) { *generated_sym_count = 0; perror("malloc flag_row_syms"); return NULL; }
    int current_sym_idx = 0;

    double time_for_initial_padding = total_time_for_this_flag_segment * 0.15;
    double time_for_flag_pixels_block = total_time_for_this_flag_segment * 0.70;
    double time_for_final_padding = total_time_for_this_flag_segment * 0.15;
    double single_flag_pixel_tone_duration = time_for_flag_pixels_block / num_flag_pixels_in_row;

    syms[current_sym_idx] = create_tone_symbol(time_for_initial_padding, SSTV_FLAG_PAD_SYNC_FREQ, 1.0, 0.0);
    if (!syms[current_sym_idx++]) goto cleanup_flag_row_error;

    for (int i = 0; i < num_flag_pixels_in_row; ++i) {
        double freq = SSTV_FLAG_PIXEL_FREQ_MIN + SSTV_FLAG_PIXEL_FREQ_RANGE * flag_pixel_row_data[i] / 255.0;
        syms[current_sym_idx] = create_tone_symbol(single_flag_pixel_tone_duration, freq, 1.0, 0.0);
        if (!syms[current_sym_idx++]) goto cleanup_flag_row_error;
    }

    syms[current_sym_idx] = create_tone_symbol(time_for_final_padding, SSTV_FLAG_PAD_SYNC_FREQ, 1.0, 0.0);
    if (!syms[current_sym_idx++]) goto cleanup_flag_row_error;
    
    assert(current_sym_idx == *generated_sym_count);
    return syms;

cleanup_flag_row_error:
    free_audio_symbols_array(syms, current_sym_idx);
    *generated_sym_count = 0;
    return NULL;
}


AudioSymbol** generate_image_data_symbols(
    const char* cover_image_filename,
    const char* flag_image_filename,
    int* total_image_symbols_count
) {
    int cover_w, cover_h, cover_channels_file;
    uint8_t* cover_img_data = stbi_load(cover_image_filename, &cover_w, &cover_h, &cover_channels_file, 0);
    if (!cover_img_data) {
        fprintf(stderr, "ERRO: %s não foi encontrado ou não pôde ser carregado.\n", cover_image_filename);
        *total_image_symbols_count = 0;
        return NULL;
    }
    if (cover_w != COVER_IMG_WIDTH || cover_h != COVER_IMG_HEIGHT) {
        fprintf(stderr, "Aviso: Dimensões de %s (%dx%d) diferem do esperado (%dx%d).\n",
                cover_image_filename, cover_w, cover_h, COVER_IMG_WIDTH, COVER_IMG_HEIGHT);
    }
    int actual_cover_channels_to_use = (cover_channels_file >= 3) ? 3 : cover_channels_file;


    int flag_w_file, flag_h_file, flag_channels_file;
    uint8_t* flag_img_data = stbi_load(flag_image_filename, &flag_w_file, &flag_h_file, &flag_channels_file, 1); // (grayscale)
    if (!flag_img_data) {
        fprintf(stderr, "ERRO: %s não foi encontrado ou não pôde ser carregado.\n", flag_image_filename);
        stbi_image_free(cover_img_data);
        *total_image_symbols_count = 0;
        return NULL;
    }
    if (flag_w_file != FLAG_IMG_WIDTH || flag_h_file != FLAG_IMG_HEIGHT) {
         fprintf(stderr, "Aviso: Dimensões de %s (%dx%d) diferem do esperado (%dx%d) para a flag.\n",
                flag_image_filename, flag_w_file, flag_h_file, FLAG_IMG_WIDTH, FLAG_IMG_HEIGHT);
    }

    int max_symbols_needed = 0;
    for (int y_coord = 0; y_coord < COVER_IMG_HEIGHT; ++y_coord) {
        for (int chan_idx = 0; chan_idx < 3; ++chan_idx) { // R, G, B
            max_symbols_needed++;
            max_symbols_needed++;
            max_symbols_needed += (COVER_IMG_WIDTH - 1);
            max_symbols_needed++;
            if (y_coord >= FLAG_IMG_POS_Y && y_coord < FLAG_IMG_POS_Y + FLAG_IMG_HEIGHT) {
                max_symbols_needed += (FLAG_IMG_WIDTH + 2); // (pixels + 2 sync)
            }
        }
    }

    AudioSymbol** image_syms_array = (AudioSymbol**)malloc(max_symbols_needed * sizeof(AudioSymbol*));
    if (!image_syms_array) {
        perror("malloc image_syms_array");
        stbi_image_free(cover_img_data); stbi_image_free(flag_img_data);
        *total_image_symbols_count = 0; return NULL;
    }

    int current_sym_idx = 0;
    double time_per_cover_pixel = SSTV_COLOR_SCANLINE_DURATION / COVER_IMG_WIDTH;

    for (int y = 0; y < COVER_IMG_HEIGHT; ++y) {
        for (int chan_map_idx = 0; chan_map_idx < 3; ++chan_map_idx) { // 0=R, 1=G, 2=B
            image_syms_array[current_sym_idx] = create_tone_symbol(SSTV_HSYNC_DURATION, SSTV_HSYNC_FREQ, 1.0, 0.0);
            if (!image_syms_array[current_sym_idx++]) goto cleanup_img_data_error;

            image_syms_array[current_sym_idx] = create_tone_symbol(time_per_cover_pixel / 2.0, SSTV_PORCH_FREQ, 1.0, 0.0);
            if (!image_syms_array[current_sym_idx++]) goto cleanup_img_data_error;

            for (int x = 0; x < COVER_IMG_WIDTH - 1; ++x) {
                int current_pixel_offset = (y * cover_w + x) * cover_channels_file;
                int next_pixel_offset = (y * cover_w + x + 1) * cover_channels_file;
                
                int actual_chan_idx_for_cover = chan_map_idx;
                if (actual_chan_idx_for_cover >= cover_channels_file) {
                    actual_chan_idx_for_cover = cover_channels_file - 1;
                }
                if (actual_chan_idx_for_cover < 0) actual_chan_idx_for_cover = 0;

                uint8_t val1 = cover_img_data[current_pixel_offset + actual_chan_idx_for_cover];
                uint8_t val2 = cover_img_data[next_pixel_offset + actual_chan_idx_for_cover];

                double freq_start = SSTV_PIXEL_FREQ_MIN + SSTV_PIXEL_FREQ_RANGE * val1 / 255.0;
                double freq_end = SSTV_PIXEL_FREQ_MIN + SSTV_PIXEL_FREQ_RANGE * val2 / 255.0;
                image_syms_array[current_sym_idx] = create_sweep_symbol(time_per_cover_pixel, freq_start, freq_end, 1.0, 0.0);
                if (!image_syms_array[current_sym_idx++]) goto cleanup_img_data_error;
            }

            image_syms_array[current_sym_idx] = create_tone_symbol(time_per_cover_pixel / 2.0, SSTV_PORCH_FREQ, 1.0, 0.0);
            if (!image_syms_array[current_sym_idx++]) goto cleanup_img_data_error;

            if (y >= FLAG_IMG_POS_Y && y < FLAG_IMG_POS_Y + FLAG_IMG_HEIGHT) {
                int flag_row_y_idx = y - FLAG_IMG_POS_Y;
                uint8_t* current_flag_row_ptr = &flag_img_data[flag_row_y_idx * flag_w_file];

                int flag_segment_sym_count;
                AudioSymbol** flag_syms_for_row = convert_flag_row_to_symbols(
                    current_flag_row_ptr, FLAG_IMG_WIDTH, SSTV_FLAG_SEGMENT_TOTAL_DURATION, &flag_segment_sym_count
                );

                if (flag_syms_for_row) {
                    for (int k = 0; k < flag_segment_sym_count; ++k) {
                        if (current_sym_idx >= max_symbols_needed) {
                            fprintf(stderr, "ERRO: Tentativa de exceder max_symbols_needed ao adicionar flag.\n");
                            free_audio_symbols_array(flag_syms_for_row, flag_segment_sym_count);
                            goto cleanup_img_data_error;
                        }
                        image_syms_array[current_sym_idx++] = flag_syms_for_row[k];
                    }
                    free(flag_syms_for_row);
                } else {
                    fprintf(stderr, "Falha ao gerar símbolos para a linha da flag y=%d. Pulando inserção da flag.\n", y);
                }
            }
        }
    }

    stbi_image_free(cover_img_data);
    stbi_image_free(flag_img_data);
    *total_image_symbols_count = current_sym_idx;
    assert(current_sym_idx <= max_symbols_needed);
    return image_syms_array;

cleanup_img_data_error:
    fprintf(stderr, "ERRO: Falha de alocação em generate_image_data_symbols. Limpando...\n");
    free_audio_symbols_array(image_syms_array, current_sym_idx);
    stbi_image_free(cover_img_data);
    stbi_image_free(flag_img_data);
    *total_image_symbols_count = 0;
    return NULL;
}

AudioSymbol* create_silence_symbol(double duration) {
    Silence* s = (Silence*)malloc(sizeof(Silence));
    if (!s) { perror("malloc Silence"); return NULL; }
    s->base.duration = duration;
    s->base.type = SILENCE_SYMBOL;
    return (AudioSymbol*)s;
}

AudioSymbol* create_tone_symbol(double duration, double freq, double amp, double phase_offset_rad) {
    Tone* t = (Tone*)malloc(sizeof(Tone));
    if (!t) { perror("malloc Tone"); return NULL; }
    t->base.duration = duration;
    t->base.type = TONE_SYMBOL;
    t->frequency = freq;
    t->amplitude = amp;
    t->phase_offset = phase_offset_rad;
    return (AudioSymbol*)t;
}

AudioSymbol* create_sweep_symbol(double duration, double f_start, double f_end, double amp, double phase_offset_rad) {
    LinearSweep* sw = (LinearSweep*)malloc(sizeof(LinearSweep));
    if (!sw) { perror("malloc LinearSweep"); return NULL; }
    sw->base.duration = duration;
    sw->base.type = LINEAR_SWEEP_SYMBOL;
    sw->freqstart = f_start;
    sw->freqend = f_end;
    sw->amplitude = amp;
    sw->phase_offset = phase_offset_rad;
    return (AudioSymbol*)sw;
}

void save_wav_file(const char* filename, int samplerate_local, int16_t* data, int length) {
    if (!data || length == 0) {
        fprintf(stderr, "Dados de áudio inválidos para salvar.\n");
        return;
    }
    FILE* file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Não foi possível abrir o arquivo %s para escrita.\n", filename);
        return;
    }
    char riff_id[4] = {'R', 'I', 'F', 'F'};
    int32_t chunk_size = 36 + length * sizeof(int16_t);
    char wave_id[4] = {'W', 'A', 'V', 'E'};
    char fmt_id[4] = {'f', 'm', 't', ' '};
    int32_t subchunk1_size = 16; int16_t audio_format = 1; int16_t num_channels = 1;
    int32_t local_samplerate_val = samplerate_local;
    int32_t byte_rate = local_samplerate_val * num_channels * sizeof(int16_t);
    int16_t block_align = num_channels * sizeof(int16_t);
    int16_t bits_per_sample = sizeof(int16_t) * 8;
    char data_id[4] = {'d', 'a', 't', 'a'};
    int32_t subchunk2_size = length * sizeof(int16_t);

    fwrite(riff_id, 1, 4, file); fwrite(&chunk_size, sizeof(chunk_size), 1, file);
    fwrite(wave_id, 1, 4, file); fwrite(fmt_id, 1, 4, file);
    fwrite(&subchunk1_size, sizeof(subchunk1_size), 1, file);
    fwrite(&audio_format, sizeof(audio_format), 1, file);
    fwrite(&num_channels, sizeof(num_channels), 1, file);
    fwrite(&local_samplerate_val, sizeof(local_samplerate_val), 1, file);
    fwrite(&byte_rate, sizeof(byte_rate), 1, file);
    fwrite(&block_align, sizeof(block_align), 1, file);
    fwrite(&bits_per_sample, sizeof(bits_per_sample), 1, file);
    fwrite(data_id, 1, 4, file); fwrite(&subchunk2_size, sizeof(subchunk2_size), 1, file);
    fwrite(data, sizeof(int16_t), length, file);
    fclose(file);
    printf("Arquivo WAV salvo em: %s (%d amostras)\n", filename, length);
}

void free_audio_symbols_array(AudioSymbol** syms, int sym_count) {
    if (!syms) return;
    for (int i = 0; i < sym_count; ++i) {
        free(syms[i]);
    }
    free(syms);
}

int main() {
    printf("Iniciando geração de sinal SSTV (versão aprimorada)...\n");

    AudioSymbol* initial_silence = create_silence_symbol(SSTV_SILENCE_DURATION);
    if (!initial_silence) return 1;

    int vox_sym_count = 0;
    AudioSymbol** vox_syms = generate_vox_signal(&vox_sym_count);
    if (!vox_syms) {
        free(initial_silence);
        return 1;
    }

    int vis_sym_count = 0;
    AudioSymbol** vis_syms = generate_vis_signal(&vis_sym_count);
    if (!vis_syms) {
        free(initial_silence);
        free_audio_symbols_array(vox_syms, vox_sym_count);
        return 1;
    }

    int image_data_sym_count = 0;
    AudioSymbol** image_data_syms = generate_image_data_symbols(COVER_IMG_FILENAME, FLAG_IMG_FILENAME, &image_data_sym_count);
    if (!image_data_syms) {
        free(initial_silence);
        free_audio_symbols_array(vox_syms, vox_sym_count);
        free_audio_symbols_array(vis_syms, vis_sym_count);
        return 1;
    }

    int eof_sym_count = 0;
    AudioSymbol** eof_syms = generate_eof_signal(&eof_sym_count);
    if (!eof_syms) {
        free(initial_silence);
        free_audio_symbols_array(vox_syms, vox_sym_count);
        free_audio_symbols_array(vis_syms, vis_sym_count);
        free_audio_symbols_array(image_data_syms, image_data_sym_count);
        return 1;
    }

    AudioSymbol* final_silence = create_silence_symbol(SSTV_SILENCE_DURATION);
    if (!final_silence) {
        perror("malloc final_silence");
        free(initial_silence);
        free_audio_symbols_array(vox_syms, vox_sym_count);
        free_audio_symbols_array(vis_syms, vis_sym_count);
        free_audio_symbols_array(image_data_syms, image_data_sym_count);
        free_audio_symbols_array(eof_syms, eof_sym_count);
        return 1;
    }


    int total_sstv_symbols = 1 + vox_sym_count + vis_sym_count + image_data_sym_count + eof_sym_count + 1;
    AudioSymbol** sstv_signal_array = (AudioSymbol**)malloc(total_sstv_symbols * sizeof(AudioSymbol*));
    if (!sstv_signal_array) {
        perror("malloc sstv_signal_array");
        free(initial_silence);
        free_audio_symbols_array(vox_syms, vox_sym_count);
        free_audio_symbols_array(vis_syms, vis_sym_count);
        free_audio_symbols_array(image_data_syms, image_data_sym_count);
        free_audio_symbols_array(eof_syms, eof_sym_count);
        free(final_silence);
        return 1;
    }

    int current_sstv_idx = 0;
    sstv_signal_array[current_sstv_idx++] = initial_silence;
    for (int i = 0; i < vox_sym_count; ++i) sstv_signal_array[current_sstv_idx++] = vox_syms[i];
    for (int i = 0; i < vis_sym_count; ++i) sstv_signal_array[current_sstv_idx++] = vis_syms[i];
    for (int i = 0; i < image_data_sym_count; ++i) sstv_signal_array[current_sstv_idx++] = image_data_syms[i];
    for (int i = 0; i < eof_sym_count; ++i) sstv_signal_array[current_sstv_idx++] = eof_syms[i];
    sstv_signal_array[current_sstv_idx++] = final_silence;
    assert(current_sstv_idx == total_sstv_symbols);

    printf("Gerando amostras WAV (%d símbolos totais)...\n", total_sstv_symbols);
    int wav_output_length;
    int16_t* wav_data_output = generate_wav(sstv_signal_array, total_sstv_symbols, SAMPLERATE, &wav_output_length);

    if (wav_data_output && wav_output_length > 0) {
        save_wav_file(OUTPUT_FILENAME, SAMPLERATE, wav_data_output, wav_output_length);
        free(wav_data_output);
    } else {
        fprintf(stderr, "Falha ao gerar dados WAV ou dados WAV vazios.\n");
        free(wav_data_output);
    }

    free(initial_silence);
    free_audio_symbols_array(vox_syms, vox_sym_count);
    free_audio_symbols_array(vis_syms, vis_sym_count);
    free_audio_symbols_array(image_data_syms, image_data_sym_count);
    free_audio_symbols_array(eof_syms, eof_sym_count);
    free(final_silence);

    free(sstv_signal_array);

    printf("Concluído.\n");
    return 0;
}
