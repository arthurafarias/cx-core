// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/containers/arguments.hpp>
#include <cx/core/containers/yaml.hpp>
#include <cx/core/testing/test_group.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <sstream>
#include <string>

namespace cx::core::testing {

inline test_group containers_yaml_tests{
    "containers.yaml",
    {
        {"every alternative survives block and flow text with its type", [](test_context &ctx) {
           using containers::variant;
           namespace yaml = containers::yaml;
           const std::map<std::string, variant> document{
               {"flag", true},
               {"rank", 8},
               {"big", std::int64_t{1} << 40},
               {"rate", 1e-4},
               {"whole", 2.0},
               {"nothing", nullptr},
               {"text", std::string("plain")},
               {"looks_like_bool", std::string("true")},
               {"looks_like_number", std::string("007")},
               {"empty", std::string()},
               {"list", std::deque<variant>{1, std::string("a,b"), std::deque<variant>{2.5}}},
               {"nested", std::map<std::string, variant>{{"k", std::string("v: w")}}},
           };
           const variant whole = document;
           ctx.check(yaml::decode(yaml::render(whole)) == whole, "block text reads back equal, alternative for alternative");
           ctx.check(yaml::decode(yaml::flow(whole)) == whole, "flow text reads back equal");
           ctx.check(yaml::decode("4").is<int>() && yaml::decode("4.0").is<double>() && yaml::decode("1099511627776").is<std::int64_t>(),
                     "an integer is int when it fits one, int64_t otherwise; a fraction makes a double");
         }},
        {"a property map is records of name, option, description, value", [](test_context &ctx) {
           using containers::property;
           using containers::property_map;
           namespace yaml = containers::yaml;
           const property_map values{{"training.lora_rank", property{8, "lora_rank", "lora-rank", "Rank of the LoRA update."}},
                                     {"model.name", property{std::string("1.5"), "name", "", ""}}};
           ctx.check(yaml::decode_properties(yaml::render(values)) == values, "block round trip keeps metadata and types");
           ctx.check(yaml::render(values).starts_with("model.name:\n  name: name\n  option: \"\"\n  description: \"\"\n  value: \"1.5\"\n"),
                     "metadata first, value last, ambiguous strings quoted");
           for (const char *broken : {"a: 1", "a: {name: x}", "a: {value: 1, colour: red}", "[1]"}) {
             bool threw = false;
             try {
               (void)yaml::decode_properties(broken);
             } catch (const yaml::error &) {
               threw = true;
             }
             ctx.check(threw, broken);
           }
         }},
        {"arguments: records render as tokens and come back; overrides keep a property's type", [](test_context &ctx) {
           using containers::property;
           using containers::property_map;
           namespace arguments = containers::arguments;
           property_map values{{"training.lora_rank", property{8, "lora_rank", "lora-rank", "Rank, of the update."}},
                               {"training.learning_rate", property{1e-4, "learning_rate", "", ""}},
                               {"model.name", property{std::string("base"), "name", "", ""}},
                               {"training.resume", property{false, "resume", "", ""}}};
           property_map copy;
           arguments::apply(copy, arguments::from(values));
           ctx.check(copy == values, "--property tokens carry complete records");

           arguments::apply(values, {"--lora-rank=16", "--training.learning_rate=1", "--model.name=007", "--training.resume=true",
                                     "--fresh=[1, 2]"});
           ctx.check(values.at("training.lora_rank").as<int>() == 16, "an option alias addresses its property");
           ctx.check(values.at("training.learning_rate").as<double>() == 1.0, "a whole number stays a double in a double property");
           ctx.check(values.at("model.name").as<std::string>() == "007", "a string property takes the raw text");
           ctx.check(values.at("training.resume").as<bool>(), "a bool");
           ctx.check(values.at("fresh").as<std::deque<containers::variant>>().size() == 2 && values.at("fresh").name == "fresh",
                     "an unknown key becomes a new property, typed by its YAML");

           const property_map before = values;
           for (const std::vector<std::string> &batch :
                {std::vector<std::string>{"--model.name=ok", "--lora-rank=1.5"}, {"--training.resume=yes"}, {"--training.learning_rate=fast"},
                 {"positional"}, {"--=x"}, {"--novalue"}}) {
             bool threw = false;
             try {
               arguments::apply(values, batch);
             } catch (const std::exception &) {
               threw = true;
             }
             ctx.check(threw && values == before, "a malformed batch throws and leaves the map as it was");
           }

           std::istringstream line("serve --port=8080 'a b' \"c d\" e\\ f");
           const auto tokens = arguments::read(line);
           ctx.check(tokens.size() == 5 && tokens[2] == "a b" && tokens[3] == "c d" && tokens[4] == "e f", "read() splits like a POSIX shell");
           ctx.check(arguments::quote_argument("it's") == "'it'\\''s'", "quote_argument survives a single quote");
         }},
    }};

} // namespace cx::core::testing
