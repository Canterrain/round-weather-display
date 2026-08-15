#include "location_scoring.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  const char *abbr;
  const char *name;
} state_name_mapping_t;

static const state_name_mapping_t US_STATE_MAPPINGS[] = {
  {"AL", "Alabama"},
  {"AK", "Alaska"},
  {"AZ", "Arizona"},
  {"AR", "Arkansas"},
  {"CA", "California"},
  {"CO", "Colorado"},
  {"CT", "Connecticut"},
  {"DE", "Delaware"},
  {"FL", "Florida"},
  {"GA", "Georgia"},
  {"HI", "Hawaii"},
  {"ID", "Idaho"},
  {"IL", "Illinois"},
  {"IN", "Indiana"},
  {"IA", "Iowa"},
  {"KS", "Kansas"},
  {"KY", "Kentucky"},
  {"LA", "Louisiana"},
  {"ME", "Maine"},
  {"MD", "Maryland"},
  {"MA", "Massachusetts"},
  {"MI", "Michigan"},
  {"MN", "Minnesota"},
  {"MS", "Mississippi"},
  {"MO", "Missouri"},
  {"MT", "Montana"},
  {"NE", "Nebraska"},
  {"NV", "Nevada"},
  {"NH", "New Hampshire"},
  {"NJ", "New Jersey"},
  {"NM", "New Mexico"},
  {"NY", "New York"},
  {"NC", "North Carolina"},
  {"ND", "North Dakota"},
  {"OH", "Ohio"},
  {"OK", "Oklahoma"},
  {"OR", "Oregon"},
  {"PA", "Pennsylvania"},
  {"RI", "Rhode Island"},
  {"SC", "South Carolina"},
  {"SD", "South Dakota"},
  {"TN", "Tennessee"},
  {"TX", "Texas"},
  {"UT", "Utah"},
  {"VT", "Vermont"},
  {"VA", "Virginia"},
  {"WA", "Washington"},
  {"WV", "West Virginia"},
  {"WI", "Wisconsin"},
  {"WY", "Wyoming"},
  {"DC", "District of Columbia"},
};

static void copy_string(char *dest, size_t dest_size, const char *src)
{
  if (dest == NULL || dest_size == 0) {
    return;
  }

  if (src == NULL) {
    dest[0] = '\0';
    return;
  }

  snprintf(dest, dest_size, "%s", src);
}

static bool strings_equal_ignore_case(const char *a, const char *b)
{
  if (a == NULL || b == NULL) {
    return false;
  }

  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
      return false;
    }
    a++;
    b++;
  }

  return *a == '\0' && *b == '\0';
}

static bool string_contains_ignore_case(const char *haystack, const char *needle)
{
  if (haystack == NULL || needle == NULL || needle[0] == '\0') {
    return false;
  }

  size_t needle_len = strlen(needle);
  size_t haystack_len = strlen(haystack);
  if (needle_len > haystack_len) {
    return false;
  }

  for (size_t offset = 0; offset + needle_len <= haystack_len; ++offset) {
    bool match = true;
    for (size_t i = 0; i < needle_len; ++i) {
      if (tolower((unsigned char) haystack[offset + i]) != tolower((unsigned char) needle[i])) {
        match = false;
        break;
      }
    }

    if (match) {
      return true;
    }
  }

  return false;
}

static bool is_alpha2_code(const char *value)
{
  if (value == NULL || strlen(value) != 2) {
    return false;
  }

  return isalpha((unsigned char) value[0]) && isalpha((unsigned char) value[1]);
}

static void trim_segment(const char *start, size_t length, char *output, size_t output_size)
{
  if (output == NULL || output_size == 0) {
    return;
  }

  while (length > 0 && isspace((unsigned char) *start)) {
    start++;
    length--;
  }

  while (length > 0 && isspace((unsigned char) start[length - 1])) {
    length--;
  }

  if (length >= output_size) {
    length = output_size - 1;
  }

  memcpy(output, start, length);
  output[length] = '\0';
}

static void resolve_state_name(const char *state, char *output, size_t output_size)
{
  if (output == NULL || output_size == 0) {
    return;
  }

  output[0] = '\0';
  if (state == NULL || state[0] == '\0') {
    return;
  }

  if (strlen(state) == 2) {
    for (size_t i = 0; i < (sizeof(US_STATE_MAPPINGS) / sizeof(US_STATE_MAPPINGS[0])); ++i) {
      if (strings_equal_ignore_case(state, US_STATE_MAPPINGS[i].abbr)) {
        copy_string(output, output_size, US_STATE_MAPPINGS[i].name);
        return;
      }
    }
  }

  copy_string(output, output_size, state);
}

void location_scoring_parse_query(const char *raw_query, location_query_parts_t *out_parts)
{
  if (out_parts == NULL) {
    return;
  }

  memset(out_parts, 0, sizeof(*out_parts));
  if (raw_query == NULL) {
    return;
  }

  const char *trimmed_start = raw_query;
  while (*trimmed_start != '\0' && isspace((unsigned char) *trimmed_start)) {
    trimmed_start++;
  }
  size_t trimmed_length = strlen(trimmed_start);
  while (trimmed_length > 0 && isspace((unsigned char) trimmed_start[trimmed_length - 1])) {
    trimmed_length--;
  }

  const char *segment_start = trimmed_start;
  const char *query_end = trimmed_start + trimmed_length;
  size_t segment_index = 0;

  for (const char *cursor = trimmed_start; ; ++cursor) {
    bool at_boundary = (cursor == query_end) || (cursor < query_end && *cursor == ',');
    if (!at_boundary) {
      continue;
    }

    size_t segment_length = (size_t) (cursor - segment_start);
    if (segment_index == 0) {
      trim_segment(segment_start, segment_length, out_parts->name, sizeof(out_parts->name));
    } else if (segment_index == 1) {
      trim_segment(segment_start, segment_length, out_parts->state, sizeof(out_parts->state));
    } else if (segment_index == 2) {
      trim_segment(segment_start, segment_length, out_parts->country, sizeof(out_parts->country));
    }

    segment_index++;
    if (cursor == query_end || segment_index >= 3) {
      break;
    }

    segment_start = cursor + 1;
  }

  out_parts->country_is_alpha2 = is_alpha2_code(out_parts->country);
  resolve_state_name(out_parts->state, out_parts->state_name, sizeof(out_parts->state_name));
}

void location_scoring_format_label(
  const char *name,
  const char *admin1,
  const char *country_code,
  char *output,
  size_t output_size
)
{
  if (output == NULL || output_size == 0) {
    return;
  }

  const char *parts[3] = { name, admin1, country_code };
  size_t offset = 0;
  bool wrote_any = false;
  output[0] = '\0';

  for (size_t i = 0; i < 3; ++i) {
    if (parts[i] == NULL || parts[i][0] == '\0' || offset >= output_size - 1) {
      continue;
    }

    int written = snprintf(
      output + offset,
      output_size - offset,
      "%s%s",
      wrote_any ? ", " : "",
      parts[i]
    );
    if (written < 0) {
      break;
    }

    size_t remaining = output_size - offset - 1;
    offset += ((size_t) written < remaining) ? (size_t) written : remaining;
    wrote_any = true;
  }
}

static int result_score(cJSON *result, const location_query_parts_t *query_parts)
{
  if (!cJSON_IsObject(result) || query_parts == NULL) {
    return 0;
  }

  int score = 0;

  cJSON *name = cJSON_GetObjectItemCaseSensitive(result, "name");
  cJSON *admin1 = cJSON_GetObjectItemCaseSensitive(result, "admin1");
  cJSON *country_code = cJSON_GetObjectItemCaseSensitive(result, "country_code");
  cJSON *population = cJSON_GetObjectItemCaseSensitive(result, "population");

  const char *result_name = cJSON_IsString(name) ? name->valuestring : "";
  const char *result_admin1 = cJSON_IsString(admin1) ? admin1->valuestring : "";
  const char *result_country_code = cJSON_IsString(country_code) ? country_code->valuestring : "";

  if (strings_equal_ignore_case(result_name, query_parts->name)) {
    score += 100;
  } else if (string_contains_ignore_case(result_name, query_parts->name)) {
    score += 30;
  }

  if (query_parts->state_name[0] != '\0'
      && strings_equal_ignore_case(result_admin1, query_parts->state_name)) {
    score += 80;
  }

  if (query_parts->country_is_alpha2
      && strings_equal_ignore_case(result_country_code, query_parts->country)) {
    score += 20;
  }

  int population_value = cJSON_IsNumber(population) ? (int) cJSON_GetNumberValue(population) : 0;
  if (population_value < 0) {
    population_value = 0;
  }

  int population_score = population_value / 1000;
  if (population_score > 25) {
    population_score = 25;
  }
  score += population_score;

  return score;
}

cJSON *location_scoring_select_best_result(cJSON *results, const location_query_parts_t *query_parts)
{
  if (!cJSON_IsArray(results) || query_parts == NULL) {
    return NULL;
  }

  cJSON *best_result = NULL;
  int best_score = -2147483647;
  cJSON *result = NULL;
  cJSON_ArrayForEach(result, results) {
    int score = result_score(result, query_parts);
    if (best_result == NULL || score > best_score) {
      best_result = result;
      best_score = score;
    }
  }

  return best_result;
}
