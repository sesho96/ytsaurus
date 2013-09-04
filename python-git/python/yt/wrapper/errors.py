import errors_config
from yt.common import YtError

import simplejson as json

class YtOperationFailedError(YtError):
    """
    Represents error that occurs when we synchronously wait operation that fails.
    """
    pass

class YtResponseError(YtError):
    """
    Represents error that occurs when we have error in HTTP response.
    """
    def __init__(self, url, headers, error):
        self.url = url
        self.headers = headers
        self.error = error

    def __str__(self):
        return "Response to request {0} with headers {1} contains error:\n{2}".\
                  format(self.url, self.headers, format_error(self.error))

    def __repr__(self):
        return self.__str__()

    def is_resolve_error(self):
        return int(self.error["code"]) == 500

    def is_access_denied(self):
        return int(self.error["code"]) == 901

    def is_concurrent_transaction_lock_conflict(self):
        return int(self.error["code"]) == 402

class YtNetworkError(YtError):
    """
    Represents an error occured while sending an HTTP request.
    Typically it wraps some underlying error.
    """
    pass

class YtProxyUnavailable(YtError):
    """
    Represents an error occured when proxy response that it is under heavy load.
    """
    pass

class YtTokenError(YtError):
    pass

class YtFormatError(YtError):
    pass


def format_error(error, indent=0):
    if errors_config.ERROR_FORMAT == "json":
        return json.dumps(error)
    elif errors_config.ERROR_FORMAT == "json_pretty":
        return json.dumps(error, indent=2)
    elif errors_config.ERROR_FORMAT == "text":
        return pretty_format(error)
    else:
        raise YtError("Incorrect error format: " + errors_config.ERROR_FORMAT)

def pretty_format(error, indent=0):
    def format_attribute(name, value):
        return (" " * (indent + 4)) + "%-15s %s" % (name, value)

    lines = []
    if "message" in error:
        lines.append(error["message"])

    if "code" in error and int(error["code"]) != 1:
        lines.append(format_attribute("code", error["code"]))

    attributes = error["attributes"]

    origin_keys = ["host", "datetime", "pid", "tid"]
    if all(key in attributes for key in origin_keys):
        lines.append(
            format_attribute(
                "origin",
                "%s in %s (pid %d, tid %x)" % (
                    attributes["host"],
                    attributes["datetime"],
                    attributes["pid"],
                    attributes["tid"])))

    location_keys = ["file", "line"]
    if all(key in attributes for key in location_keys):
        lines.append(format_attribute("location", "%s:%d" % (attributes["file"], attributes["line"])))

    for key, value in attributes.items():
        if key in origin_keys or key in location_keys:
            continue
        lines.append(format_attribute(key, str(value)))

    result = " " * indent + (" " * (indent + 4) + "\n").join(lines)
    if "inner_errors" in error:
        for inner_error in error["inner_errors"]:
            result += "\n" + format_error(inner_error, indent + 2)

    return result


