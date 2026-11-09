#ifndef SATORU_MERMAID_PACKET_H
#define SATORU_MERMAID_PACKET_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `packet-beta` / `packet` source (first line included) to SVG: fields of bits in rows of 32.
bool render_packet(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_PACKET_H
