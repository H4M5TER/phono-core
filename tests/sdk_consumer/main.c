#include <phono_api.h>
#include <string.h>

int main(void) {
    return strcmp(phono_error_name(PHONO_OK), "ok") != 0;
}
