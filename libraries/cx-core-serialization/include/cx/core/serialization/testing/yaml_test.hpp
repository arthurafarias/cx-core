// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/serialization/yaml.hpp>
#include <cx/core/testing/test_group.hpp>

#include <cmath>
#include <limits>
#include <string>

namespace cx::core::testing {

inline test_group yaml_tests{
    "yaml",
    {
        {"block mappings, sequences, compact items, scalars and comments", [](test_context &ctx) {
           namespace yaml = serialization::yaml;
           const yaml::value document = yaml::parse("hparams:\n"
                                                    "  n_layer: 2   # a comment\n"
                                                    "  eps: 1e-5\n"
                                                    "  name: \"a: b\"\n"
                                                    "layers:\n"
                                                    "  - name: embed\n"
                                                    "    inputs:\n"
                                                    "      - tokens\n"
                                                    "  - plain\n"
                                                    "empty: {}\n"
                                                    "nothing: ~\n");
           ctx.check(document.at("hparams").at("n_layer").as_int() == 2, "an integer");
           ctx.check(document.at("hparams").at("eps").as_double() == 1e-5, "an exponent without a fraction is a float");
           ctx.check(document.at("hparams").at("name").as_string() == "a: b", "a quoted scalar keeps its colon");
           ctx.check(document.at("layers")[0].at("inputs")[0].as_string() == "tokens", "a compact `- key: value` item");
           ctx.check(document.at("layers")[1].as_string() == "plain", "a scalar item");
           ctx.check(document.at("empty").is_mapping() && document.at("nothing").is_null(), "{} and ~");
         }},
        {"flow collections on one line, nested, with quoted and tagged scalars", [](test_context &ctx) {
           namespace yaml = serialization::yaml;
           const yaml::value record =
               yaml::parse("{training.lora_rank: {name: lora_rank, option: \"\", description: \"Rank, of LoRA\", value: 8}}");
           const yaml::value &inner = record.at("training.lora_rank");
           ctx.check(inner.at("value").as_int() == 8 && inner.at("option").as_string().empty(), "a nested flow mapping");
           ctx.check(inner.at("description").as_string() == "Rank, of LoRA", "a comma inside quotes does not split");
           const yaml::value list = yaml::parse("key: [1, [2.5, x], {a: true}, !!float 3, 'it''s']");
           const yaml::value &items = list.at("key");
           ctx.check(items.size() == 5 && items[1][1].as_string() == "x" && items[2].at("a").as_bool(), "sequences nest");
           ctx.check(items[3].is_float() && items[3].as_double() == 3.0, "!!float pins a whole number to a float");
           ctx.check(items[4].as_string() == "it's", "a doubled single quote is one quote");
           ctx.check(yaml::parse("- {a: 1}\n- [x]").size() == 2, "a flow collection as a block sequence item");
         }},
        {"a document may be one scalar; tags pin its type", [](test_context &ctx) {
           namespace yaml = serialization::yaml;
           ctx.check(yaml::parse("4").as_int() == 4 && yaml::parse("hello world").as_string() == "hello world", "plain");
           ctx.check(yaml::parse("").is_null() && yaml::parse("# only a comment\n").is_null(), "an empty document is null");
           ctx.check(yaml::parse("! 007").as_string() == "007" && yaml::parse("!!str true").as_string() == "true",
                     "the non-specific tag and !!str keep text as text");
           ctx.check(yaml::parse("!!float \".inf\"").as_double() == std::numeric_limits<double>::infinity(), "a tagged quoted scalar");
           ctx.check(std::isnan(yaml::parse(".nan").as_double()), ".nan");
           ctx.check(yaml::parse("99999999999999999999").is_float(), "an integer too large for one is the nearest double");
           ctx.check(yaml::parse("\"\\x41\\u00e9\\n\"").as_string() == "A\xc3\xa9\n", "\\x and \\u escapes become UTF-8");
         }},
        {"dump and flow are parse's exact inverse", [](test_context &ctx) {
           namespace yaml = serialization::yaml;
           yaml::value document = yaml::value::mapping();
           document.set("whole", yaml::value::floating(1.0));
           document.set("tenth", yaml::value::floating(0.1));
           document.set("tiny", yaml::value::floating(1e-300));
           document.set("inf", yaml::value::floating(-std::numeric_limits<double>::infinity()));
           document.set("count", yaml::value::integer(-42));
           document.set("yes", yaml::value::boolean(true));
           document.set("nothing", yaml::value::null());
           for (const char *text : {"true", "007", "1.5", "~", "", "null", " padded ", "a: b", "x #y", "- item", "{a}", "[b",
                                    "a,b", "line\nbreak", "tab\there", "\x01\x1b[2J", "!tag", "&anchor", "*alias", "%dir",
                                    "plain text", "http://host:80/path", "é😀"})
             document.set(std::string("s:") + text, yaml::value::string(text));
           document.set("list", yaml::value::sequence({yaml::value::string("a,b"), yaml::value::sequence({yaml::value::integer(1)}),
                                                       yaml::value::mapping({{"k", yaml::value::string("v")}})}));
           document.set("empty_list", yaml::value::sequence());
           ctx.check(yaml::parse(yaml::dump(document)) == document, "block: dump then parse is the identity");
           ctx.check(yaml::parse(yaml::flow(document)) == document, "flow: flow then parse is the identity");
           ctx.check(yaml::flow(document).find('\n') == std::string::npos, "flow is one line");
           ctx.check(yaml::dump(yaml::parse("x: 1.0")) == "x: 1.0\n" && yaml::flow(yaml::parse("[1, 1.0]")) == "[1, 1.0]",
                     "a whole float keeps its fraction, an integer has none");
           ctx.check(yaml::flow(yaml::value::string("http://host:80/path")) == "http://host:80/path", "a colon inside a word stays plain");
         }},
        {"what is outside the subset is refused with a line number, never misread", [](test_context &ctx) {
           namespace yaml = serialization::yaml;
           for (const char *broken : {"a: &x 1", "a: *x", "a: !custom 1", "a: |\n  text", "a: >\n  text", "a: 1\n---\nb: 2",
                                      "%YAML 1.2\na: 1", "a: 1\na: 2", "{a: 1, a: 2}", "\tb: 1", "a: {b: 1", "a: [1, 2", "a: [1 2}",
                                      "a: \"open", "a: {b: 1} trailing", "a: !!int x", "a: !!float true", "a:\n  b: 1\n c: 2"}) {
             bool threw = false;
             try {
               (void)yaml::parse(broken);
             } catch (const yaml::error &failure) {
               threw = std::string(failure.what()).starts_with("yaml:");
             }
             ctx.check(threw, broken);
           }
           std::string deep;
           for (int i = 0; i < 200; ++i)
             deep += "[";
           bool threw = false;
           try {
             (void)yaml::parse(deep);
           } catch (const yaml::error &) {
             threw = true;
           }
           ctx.check(threw, "nesting past the limit is refused rather than overflowing the stack");
         }},
    }};

} // namespace cx::core::testing
