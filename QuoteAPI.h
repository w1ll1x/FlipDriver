#ifndef QUOTE_API_H
#define QUOTE_API_H

#include <Arduino.h>

namespace QuoteAPI {

    // Fetches the next quote from the sign endpoint.
    // Populates message with the uppercased quote text.
    // Returns true on success, false on any network or parse error.
    bool fetch(String &message);

}

#endif // QUOTE_API_H
