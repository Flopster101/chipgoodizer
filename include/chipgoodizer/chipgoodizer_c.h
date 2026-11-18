/*
 * Chipgoodizer C ABI: prepare/reset/process on interleaved floats, plus an
 * index-addressed parameter table. NULL handles no-op.
 */
#ifndef CHIPGOODIZER_C_H
#define CHIPGOODIZER_C_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct cg_effect cg_effect;

/* Numeric values are part of the ABI. */
typedef enum cg_param_type
{
    CG_PARAM_BOOL = 0,
    CG_PARAM_INT = 1,
    CG_PARAM_FLOAT = 2,
    CG_PARAM_ENUM = 3
} cg_param_type;

typedef struct cg_param_info
{
    const char *id;
    const char *label;
    cg_param_type type;
    float min_value;
    float max_value;
    float default_value;
    float step;
} cg_param_info;

typedef enum cg_preset
{
    CG_PRESET_COMMERCIAL = 0,
    CG_PRESET_SCOPE_SAFE = 1,
    CG_PRESET_WARM = 2,
    CG_PRESET_CLUB = 3
} cg_preset;

/* Presets write every parameter. Index order is stable. */
int cg_preset_count(void);
const char *cg_preset_id(int index);    /* stable spelling for persistence, NULL when out of range */
const char *cg_preset_label(int index); /* display name, NULL when out of range */

/* Creates an instance with the commercial preset loaded. Free with cg_destroy. */
cg_effect *cg_create(void);
void cg_destroy(cg_effect *effect);

/* Sets the sample rate and clears state. channels is informational. */
void cg_prepare(cg_effect *effect, double sample_rate, int channels);
void cg_reset(cg_effect *effect);

/* In-place interleaved. channels == 1 averages the stereo result back down. */
void cg_process_interleaved(cg_effect *effect, float *interleaved, int frames, int channels);

/* Parameter access. Out-of-range indices and non-finite values are ignored. */
int cg_param_count(void);
const cg_param_info *cg_param_at(int index);
int cg_param_index(const char *id); /* -1 when unknown */

float cg_get_param(const cg_effect *effect, int index);
void cg_set_param(cg_effect *effect, int index, float value);
void cg_set_param_by_id(cg_effect *effect, const char *id, float value);

void cg_apply_preset(cg_effect *effect, cg_preset preset);

/* Always 0. */
double cg_latency_frames(const cg_effect *effect);

#ifdef __cplusplus
}
#endif

#endif /* CHIPGOODIZER_C_H */
