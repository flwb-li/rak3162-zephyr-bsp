#ifndef HW_AT_H_
#define HW_AT_H_

#include <stddef.h>

/**
 * @brief AT command form decoded from input line.
 *
 * The parser supports a RUI3-like syntax:
 * - EXEC: `AT+XXX` (or `ATZ`, `ATR`, `ATE0/1` shorthand)
 * - HELP: `AT+XXX?`
 * - GET:  `AT+XXX=?`
 * - SET:  `AT+XXX=<args>`
 */
enum hw_at_form {
    HW_AT_FORM_EXEC = 0,
    HW_AT_FORM_HELP,
    HW_AT_FORM_GET,
    HW_AT_FORM_SET,
};

/**
 * @brief Parsed AT request passed to command handlers.
 *
 * Notes:
 * - `raw` points to the same line buffer passed into the parser and may be
 *   modified in-place (trimmed, uppercased, and NUL-terminated segments).
 * - `args` is non-NULL only for SET form.
 * - `name` is the command name without the leading "AT+" prefix and is
 *   uppercased by the parser.
 */
struct hw_at_request {
    const char *name;
    enum hw_at_form form;
    const char *args;
    const char *raw;
};

/**
 * @brief AT command handler function.
 *
 * @param req Parsed AT request. Must not be NULL.
 *
 * @return 0 on success; negative errno-style code on failure.
 *         Note: Most handlers print their own response lines; the return value
 *         is currently used for debug logging only.
 */
typedef int (*hw_at_handler_t)(const struct hw_at_request *req);

/**
 * @brief Register an AT command.
 *
 * @param name  Command name without the leading "AT+" prefix (e.g. "PFREQ").
 *              Must remain valid for the lifetime of the program (typically a
 *              string literal).
 * @param handler Handler callback for this command.
 * @param help  Optional help string used by the global help listing.
 *
 * @return 0 on success; negative errno-style code on failure.
 */
int hw_at_register_command(const char *name, hw_at_handler_t handler, const char *help);

/**
 * @brief Initialize the AT framework and register built-in commands.
 *
 * This function must be called once during boot, after storage initialization
 * if persisted parameters should be loaded.
 */
void hw_at_init(void);

int hw_at_cmd_deveui(const struct hw_at_request *req);
int hw_at_cmd_appeui(const struct hw_at_request *req);
int hw_at_cmd_p2p(const struct hw_at_request *req);
int hw_at_cmd_precv(const struct hw_at_request *req);
int hw_at_cmd_psend(const struct hw_at_request *req);
int hw_at_cmd_cw(const struct hw_at_request *req);
int hw_at_cmd_test(const struct hw_at_request *req);

int hw_at_cmd_blecw(const struct hw_at_request *req);
int hw_at_cmd_blecwstop(const struct hw_at_request *req);

/**
 * @brief Parse and execute a single AT line.
 *
 * @param line NUL-terminated string containing the full AT line.
 *             The buffer may be modified in-place during parsing.
 */
void hw_at_process_line(char *line);

/**
 * @brief Send an "OK" response line.
 */
void hw_at_resp_ok(void);

/**
 * @brief Send an error response line.
 *
 * @param err Optional string for future extensions. Currently ignored and the
 *            implementation always prints "AT_ERROR".
 */
void hw_at_resp_error(const char *err);

/**
 * @brief Send one response line terminated with CRLF.
 *
 * @param fmt printf-style format.
 */
void hw_at_resp_line(const char *fmt, ...);

#endif /* HW_AT_H_ */
