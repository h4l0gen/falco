// SPDX-License-Identifier: Apache-2.0
/*
Copyright (C) 2023 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <algorithm>

#include <list>
#include <set>
#include <string>
#include <unordered_set>

#include <filesystem>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#else
// Used in the ebpf probe path.
#define PATH_MAX 260
#endif
#include "falco_utils.h"

#include "configuration.h"
#include "logger.h"
#include <iterator>
#include <re2/re2.h>
#include <iostream>
#include <fstream>
#include "valijson/adapters/yaml_cpp_adapters.hpp"
#include "valijson/schema_parser.hpp"
#include "valijson/schema.hpp"
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

// Reference: https://digitalfortress.tech/tips/top-15-commonly-used-regex/
static re2::RE2 ip_address_re("((^\\s*((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5]))\\s*$)|(^\\s*((([0-9A-Fa-f]{1,4}:){7}([0-9A-Fa-f]{1,4}|:))|(([0-9A-Fa-f]{1,4}:){6}(:[0-9A-Fa-f]{1,4}|((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3})|:))|(([0-9A-Fa-f]{1,4}:){5}(((:[0-9A-Fa-f]{1,4}){1,2})|:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3})|:))|(([0-9A-Fa-f]{1,4}:){4}(((:[0-9A-Fa-f]{1,4}){1,3})|((:[0-9A-Fa-f]{1,4})?:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){3}(((:[0-9A-Fa-f]{1,4}){1,4})|((:[0-9A-Fa-f]{1,4}){0,2}:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){2}(((:[0-9A-Fa-f]{1,4}){1,5})|((:[0-9A-Fa-f]{1,4}){0,3}:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){1}(((:[0-9A-Fa-f]{1,4}){1,6})|((:[0-9A-Fa-f]{1,4}){0,4}:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(:(((:[0-9A-Fa-f]{1,4}){1,7})|((:[0-9A-Fa-f]{1,4}){0,5}:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:)))(%.+)?\\s*$))");

#define DEFAULT_BUF_SIZE_PRESET 4
#define DEFAULT_CPUS_FOR_EACH_SYSCALL_BUFFER 2
#define DEFAULT_DROP_FAILED_EXIT false

falco_configuration::falco_configuration():
	m_json_output(false),
	m_json_include_output_property(true),
	m_json_include_tags_property(true),
	m_rule_matching(falco_common::rule_matching::FIRST),
	m_watch_config_files(true),
	m_buffered_outputs(false),
	m_outputs_queue_capacity(DEFAULT_OUTPUTS_QUEUE_CAPACITY_UNBOUNDED_MAX_LONG_VALUE),
	m_time_format_iso_8601(false),
	m_output_timeout(2000),
	m_grpc_enabled(false),
	m_grpc_threadiness(0),
	m_webserver_enabled(false),
	m_webserver_threadiness(0),
	m_webserver_listen_port(8765),
	m_webserver_listen_address("0.0.0.0"),
	m_webserver_k8s_healthz_endpoint("/healthz"),
	m_webserver_ssl_enabled(false),
	m_syscall_evt_drop_threshold(.1),
	m_syscall_evt_drop_rate(.03333),
	m_syscall_evt_drop_max_burst(1),
	m_syscall_evt_simulate_drops(false),
	m_syscall_evt_timeout_max_consecutives(1000),
	m_falco_libs_thread_table_size(DEFAULT_FALCO_LIBS_THREAD_TABLE_SIZE),
	m_base_syscalls_repair(false),
	m_metrics_enabled(false),
	m_metrics_interval_str("5000"),
	m_metrics_interval(5000),
	m_metrics_stats_rule_enabled(false),
	m_metrics_output_file(""),
	m_metrics_flags((PPM_SCAP_STATS_KERNEL_COUNTERS | PPM_SCAP_STATS_LIBBPF_STATS | PPM_SCAP_STATS_RESOURCE_UTILIZATION | PPM_SCAP_STATS_STATE_COUNTERS)),
	m_metrics_convert_memory_to_mb(true),
	m_metrics_include_empty_values(false)
{
}

void falco_configuration::init(const std::vector<std::string>& cmdline_options)
{
	yaml_helper config;
	config.load_from_string("");
	init_cmdline_options(config, cmdline_options);
	load_yaml("default", config);
}

void falco_configuration::init(const std::string& conf_filename, const std::vector<std::string> &cmdline_options)
{
	yaml_helper config;
	try
	{
		config.load_from_file(conf_filename);
	}
	catch(const std::exception& e)
	{
		std::cerr << "Cannot read config file (" + conf_filename + "): " + e.what() + "\n";
		throw e;
	}

	init_cmdline_options(config, cmdline_options);
	load_yaml(conf_filename, config);
}

void falco_configuration::load_engine_config(const std::string& config_name, const yaml_helper& config)
{
	// Set driver mode if not already set.
	const std::unordered_map<std::string, engine_kind_t> engine_mode_lut = {
		{"kmod",engine_kind_t::KMOD},
		{"ebpf",engine_kind_t::EBPF},
		{"modern_ebpf",engine_kind_t::MODERN_EBPF},
		{"replay",engine_kind_t::REPLAY},
		{"gvisor",engine_kind_t::GVISOR},
		{"nodriver",engine_kind_t::NODRIVER},
	};

	auto driver_mode_str = config.get_scalar<std::string>("engine.kind", "kmod");
	if (engine_mode_lut.find(driver_mode_str) != engine_mode_lut.end())
	{
		m_engine_mode = engine_mode_lut.at(driver_mode_str);
	}
	else
	{
		throw std::logic_error("Error reading config file (" + config_name + "): engine.kind '"+ driver_mode_str + "' is not a valid kind.");
	}

	switch (m_engine_mode)
	{
	case engine_kind_t::KMOD:
		m_kmod.m_buf_size_preset = config.get_scalar<int16_t>("engine.kmod.buf_size_preset", DEFAULT_BUF_SIZE_PRESET);
		m_kmod.m_drop_failed_exit = config.get_scalar<bool>("engine.kmod.drop_failed_exit", DEFAULT_DROP_FAILED_EXIT);
		break;
	case engine_kind_t::EBPF:
		{
			// default value for `m_probe_path` should be `$HOME/FALCO_PROBE_BPF_FILEPATH`
			char full_path[PATH_MAX];
			const char *home = std::getenv("HOME");
			if(!home)
			{
				throw std::logic_error("Cannot get the env variable 'HOME'");
			}
			snprintf(full_path, PATH_MAX, "%s/%s", home, FALCO_PROBE_BPF_FILEPATH);
			m_ebpf.m_probe_path = config.get_scalar<std::string>("engine.ebpf.probe", std::string(full_path));
			m_ebpf.m_buf_size_preset = config.get_scalar<int16_t>("engine.ebpf.buf_size_preset", DEFAULT_BUF_SIZE_PRESET);
			m_ebpf.m_drop_failed_exit = config.get_scalar<bool>("engine.ebpf.drop_failed_exit", DEFAULT_DROP_FAILED_EXIT);
		}
		break;
	case engine_kind_t::MODERN_EBPF:
		m_modern_ebpf.m_cpus_for_each_buffer = config.get_scalar<uint16_t>("engine.modern_ebpf.cpus_for_each_buffer", DEFAULT_CPUS_FOR_EACH_SYSCALL_BUFFER);
		m_modern_ebpf.m_buf_size_preset = config.get_scalar<int16_t>("engine.modern_ebpf.buf_size_preset", DEFAULT_BUF_SIZE_PRESET);
		m_modern_ebpf.m_drop_failed_exit = config.get_scalar<bool>("engine.modern_ebpf.drop_failed_exit", DEFAULT_DROP_FAILED_EXIT);
		break;
	case engine_kind_t::REPLAY:
		m_replay.m_capture_file = config.get_scalar<std::string>("engine.replay.capture_file", "");
		if (m_replay.m_capture_file.empty())
		{
			throw std::logic_error("Error reading config file (" + config_name + "): engine.kind is 'replay' but no engine.replay.capture_file specified.");
		}
		break;
	case engine_kind_t::GVISOR:
		m_gvisor.m_config = config.get_scalar<std::string>("engine.gvisor.config", "");
		if (m_gvisor.m_config.empty())
		{
			throw std::logic_error("Error reading config file (" + config_name + "): engine.kind is 'gvisor' but no engine.gvisor.config specified.");
		}
		m_gvisor.m_root = config.get_scalar<std::string>("engine.gvisor.root", "");
		break;
	case engine_kind_t::NODRIVER:
	default:
		break;
	}
}


// std::vector<std::string> fixedSchemaKeysVec = {
// "rules_file", "engine", "engine.kind", "engine.kmod", "engine.kmod.buf_size_preset", "engine.kmod.drop_failed_exit", "engine.ebpf", "engine.ebpf.probe", "engine.ebpf.buf_size_preset", "engine.ebpf.drop_failed_exit", "engine.modern_ebpf", "engine.modern_ebpf.cpus_for_each_buffer", "engine.modern_ebpf.buf_size_preset", "engine.modern_ebpf.drop_failed_exit", "engine.replay", "engine.replay.capture_file", "engine.gvisor", "engine.gvisor.config", "engine.gvisor.root", "load_plugins", "plugins", "plugins.name", "plugins.library_path", "plugins.init_config", "plugins.init_config.maxEventSize", "plugins.init_config.webhookMaxBatchSize", "plugins.init_config.sslCertificate", "plugins.open_params", "plugins.library_path", "watch_config_files", "time_format_iso_8601", "priority", "json_output", "json_include_output_property", "json_include_tags_property", "buffered_outputs", "rule_matching", "outputs_queue", "outputs_queue.capacity", "stdout_output", "stdout_output.enabled", "syslog_output", "syslog_output.enabled", "file_output", "file_output.enabled", "file_output.keep_alive", "file_output.filename", "http_output", "http_output.enabled", "http_output.url", "http_output.user_agent", "http_output.insecure", "http_output.ca_cert", "http_output.ca_bundle", "http_output.ca_path", "http_output.mtls", "http_output.client_cert", "http_output.client_key", "http_output.echo", "http_output.compress_uploads", "http_output.keep_alive", "program_output", "program_output.enabled", "program_output.keep_alive", "program_output.program", "grpc_output", "grpc_output.enabled", "grpc", "grpc.enabled", "grpc.bind_address", "grpc.threadiness", "webserver", "webserver.enabled", "webserver.threadiness", "webserver.listen_port", "webserver.listen_address", "webserver.k8s_healthz_endpoint", "webserver.ssl_enabled", "webserver.ssl_certificate", "log_stderr", "log_syslog", "log_level", "libs_logger", "libs_logger.enabled", "libs_logger.severity", "output_timeout", "syscall_event_timeouts", "syscall_event_timeouts.max_consecutives", "syscall_event_drops", "syscall_event_drops.threshold", "syscall_event_drops.actions", "syscall_event_drops.rate", "syscall_event_drops.max_burst", "syscall_event_drops.simulate_drops", "metrics", "metrics.enabled", "metrics.interval", "metrics.output_rule", "metrics.output_file", "metrics.resource_utilization_enabled", "metrics.state_counters_enabled", "metrics.kernel_event_counters_enabled", "metrics.libbpf_stats_enabled", "metrics.convert_memory_to_mb", "metrics.include_empty_values", "syscall_buf_size_preset", "syscall_drop_failed_exit", "base_syscalls", "base_syscalls.custom_set", "base_syscalls.repair", "modern_bpf", "modern_bpf.cpus_for_each_syscall_buffer"

// };
// valijson::adapters::YamlCppArray fixedSchemaArray;

// for (const auto& key : fixedSchemaKeysVec) {
//     fixedSchemaArray.push_back(std::make_pair(key, YAML::Node()));
// } 

// valijson::adapters::YamlCppAdapter fixedSchemaAdapter(fixedSchemaArray);

// void validateKeysRecursive(const YAML::Node& node, const std::string& prefix, const std::unordered_set<std::string>& fixedSchemaKeys)
// {
//     if (node.IsMap()) {
//         for (const auto& entry : node) {
//             std::string key = prefix + entry.first.as<std::string>();

//             if (fixedSchemaKeys.find(key) == fixedSchemaKeys.end()) {
//                 throw std::logic_error("Error: Key '" + key + "' is not allowed in fixed.yaml");
//             } 

//             validateKeysRecursive(entry.second, key + ".", fixedSchemaKeys);
//         }
//     }
// }

// void validateLoadedYAML(const YAML::Node& loadedYaml, const valijson::adapters::YamlCppAdapter& fixedSchemaAdapter)
// {
//     std::unordered_set<std::string> fixedSchemaKeys;

//     for (const auto& member : fixedSchemaAdapter.begin()) {
//         fixedSchemaKeys.insert(member.first);
//     }

//     validateKeysRecursive(loadedYaml, "", fixedSchemaKeys);
// }


//    
// 
// 
// 
// 
// 

// void validateLoadedYAML(const YAML::Node& loadedYAML, const valijson::Schema& schema) {
// 	std::unordered_set<std::string> loadedYamlKeys;
// 	std::function<void(const YAML::Node&, const std::string&)> extractKeys = [&](const YAML::Node& node, const std::string& prefix) {
//         if (node.IsMap()) {
//             for (const auto& entry : node) {
//                 std::string key = prefix + entry.first.as<std::string>();
//                 loadedYamlKeys.insert(key);
//                 extractKeys(entry.second, key + ".");
//             }
//         }
//     };
//     extractKeys(loadedYaml, "");

//     // Get all keys defined in the JSON schema
//     std::unordered_set<std::string> schemaKeys;
//     schema.forEachProperty([&](const std::string& propertyName, const valijson::SchemaNode&) {
//         schemaKeys.insert(propertyName);
//     });

//     // Check if all keys in the loaded YAML are present in the JSON schema
//     for (const auto& key : loadedYamlKeys) {
//         if (schemaKeys.find(key) == schemaKeys.end()) {
//             throw std::logic_error("Error: Key '" + key + "' is not allowed in the JSON schema");
//         }
//     }
// }

// int main() {
//     // Load the JSON schema from file
//     std::ifstream schemaFile("falco_keys.json");
//     if (!schemaFile.is_open()) {
//         throw std::runtime_error("Failed to open schema file");
//     }

//     valijson::SchemaParser parser;
//     valijson::Schema schema;
//     try {
//         parser.populateSchema(schemaFile, schema);
//     } catch (const std::exception& e) {
//         throw std::runtime_error("Failed to parse schema: " + std::string(e.what()));
//     }

//     // Load the YAML document to be validated
//     YAML::Node loadedYaml = YAML::LoadFile("../../falco.yaml");

//     // Validate the loaded YAML against the schema
//     validateLoadedYAML(loadedYaml, schema);

//     std::cout << "YAML validation successful!" << std::endl;

//     return 0;
// }
void validateLoadedYAML(const YAML::Node& loadedYaml, const valijson::adapters::YamlCppAdapter& schemaAdapter) {
	valijson::Validator validator;
	validator.validate(schemaAdapter, loadedYaml);
	if (!validator.isValid()) {
		throw std::logic_error("ERROR: Loaded YAML does not conform to the schema");
	}
}

void falco_configuration::load_yaml(const std::string& config_name, const yaml_helper& config)
{
	valijson::Schema schema;
	valijson::SchemaParser parser;
	std::ifstream schemaFile("falco_keys.json");
	parser.populateSchema(schema, schemaFile);

	//Convert schema to YamlCppAdapter
	valijson::adapters::YamlCppAdapter schemaAdapter(schema);
	
	// Load the configuration from file
	YAML::Node loadedYaml = YAML::LoadFile(config_name);
	// Validate loaded YAML against schema
	try {
		validateLoadedYAML(loadedYaml, schemaAdapter);
		std::cout << "Validation successful!" << std::endl;
	} catch (const std::exception& e) {
		std::cerr << "Validation failed: " << e.what() << std::endl;
		return;
	}

	load_engine_config(config_name, config);
	m_log_level = config.get_scalar<std::string>("log_level", "info");

	std::list<std::string> rules_files;

	config.get_sequence<std::list<std::string>>(rules_files, std::string("rules_file"));

	m_rules_filenames.clear();
	m_loaded_rules_filenames.clear();
	m_loaded_rules_folders.clear();
	for(auto &file : rules_files)
	{
		// Here, we only include files that exist
		struct stat buffer;
		if(stat(file.c_str(), &buffer) == 0)
		{
			m_rules_filenames.push_back(file);
		}
	}

	m_json_output = config.get_scalar<bool>("json_output", false);
	m_json_include_output_property = config.get_scalar<bool>("json_include_output_property", true);
	m_json_include_tags_property = config.get_scalar<bool>("json_include_tags_property", true);

	m_outputs.clear();
	falco::outputs::config file_output;
	file_output.name = "file";
	if(config.get_scalar<bool>("file_output.enabled", false))
	{
		std::string filename, keep_alive;
		filename = config.get_scalar<std::string>("file_output.filename", "");
		if(filename == std::string(""))
		{
			throw std::logic_error("Error reading config file (" + config_name + "): file output enabled but no filename in configuration block");
		}
		file_output.options["filename"] = filename;

		keep_alive = config.get_scalar<std::string>("file_output.keep_alive", "");
		file_output.options["keep_alive"] = keep_alive;

		m_outputs.push_back(file_output);
	}

	falco::outputs::config stdout_output;
	stdout_output.name = "stdout";
	if(config.get_scalar<bool>("stdout_output.enabled", false))
	{
		m_outputs.push_back(stdout_output);
	}

	falco::outputs::config syslog_output;
	syslog_output.name = "syslog";
	if(config.get_scalar<bool>("syslog_output.enabled", false))
	{
		m_outputs.push_back(syslog_output);
	}

	falco::outputs::config program_output;
	program_output.name = "program";
	if(config.get_scalar<bool>("program_output.enabled", false))
	{
		std::string program, keep_alive;
		program = config.get_scalar<std::string>("program_output.program", "");
		if(program == std::string(""))
		{
			throw std::logic_error("Error reading config file (" + config_name + "): program output enabled but no program in configuration block");
		}
		program_output.options["program"] = program;

		keep_alive = config.get_scalar<std::string>("program_output.keep_alive", "");
		program_output.options["keep_alive"] = keep_alive;

		m_outputs.push_back(program_output);
	}

	falco::outputs::config http_output;
	http_output.name = "http";
	if(config.get_scalar<bool>("http_output.enabled", false))
	{
		std::string url;
		url = config.get_scalar<std::string>("http_output.url", "");

		if(url == std::string(""))
		{
			throw std::logic_error("Error reading config file (" + config_name + "): http output enabled but no url in configuration block");
		}
		http_output.options["url"] = url;

		std::string user_agent;
		user_agent = config.get_scalar<std::string>("http_output.user_agent","falcosecurity/falco");
		http_output.options["user_agent"] = user_agent;

		bool insecure;
		insecure = config.get_scalar<bool>("http_output.insecure", false);
		http_output.options["insecure"] = insecure? std::string("true") : std::string("false");

		bool echo;
		echo = config.get_scalar<bool>("http_output.echo", false);
		http_output.options["echo"] = echo? std::string("true") : std::string("false");
		
		std::string ca_cert;
		ca_cert = config.get_scalar<std::string>("http_output.ca_cert", "");
		http_output.options["ca_cert"] = ca_cert;

		std::string ca_bundle;
		ca_bundle = config.get_scalar<std::string>("http_output.ca_bundle", "");
		http_output.options["ca_bundle"] = ca_bundle;

		std::string ca_path;
		ca_path = config.get_scalar<std::string>("http_output.ca_path", "/etc/ssl/certs");
		http_output.options["ca_path"] = ca_path;

		bool mtls;
		mtls = config.get_scalar<bool>("http_output.mtls", false);
		http_output.options["mtls"] = mtls? std::string("true") : std::string("false");

		std::string client_cert;
		client_cert = config.get_scalar<std::string>("http_output.client_cert", "/etc/ssl/certs/client.crt");
		http_output.options["client_cert"] = client_cert;

		std::string client_key;
		client_key = config.get_scalar<std::string>("http_output.client_key", "/etc/ssl/certs/client.key");
		http_output.options["client_key"] = client_key;

		bool compress_uploads;
		compress_uploads = config.get_scalar<bool>("http_output.compress_uploads", false);
		http_output.options["compress_uploads"] = compress_uploads? std::string("true") : std::string("false");

		bool keep_alive;
		keep_alive = config.get_scalar<bool>("http_output.keep_alive", false);
		http_output.options["keep_alive"] = keep_alive? std::string("true") : std::string("false");

		m_outputs.push_back(http_output);
	}

	m_grpc_enabled = config.get_scalar<bool>("grpc.enabled", false);
	m_grpc_bind_address = config.get_scalar<std::string>("grpc.bind_address", "0.0.0.0:5060");
	m_grpc_threadiness = config.get_scalar<uint32_t>("grpc.threadiness", 0);
	if(m_grpc_threadiness == 0)
	{
		m_grpc_threadiness = falco::utils::hardware_concurrency();
	}
	// todo > else limit threadiness to avoid oversubscription?
	m_grpc_private_key = config.get_scalar<std::string>("grpc.private_key", "/etc/falco/certs/server.key");
	m_grpc_cert_chain = config.get_scalar<std::string>("grpc.cert_chain", "/etc/falco/certs/server.crt");
	m_grpc_root_certs = config.get_scalar<std::string>("grpc.root_certs", "/etc/falco/certs/ca.crt");

	falco::outputs::config grpc_output;
	grpc_output.name = "grpc";
	// gRPC output is enabled only if gRPC server is enabled too
	if(config.get_scalar<bool>("grpc_output.enabled", true) && m_grpc_enabled)
	{
		m_outputs.push_back(grpc_output);
	}

	m_log_level = config.get_scalar<std::string>("log_level", "info");

	falco_logger::set_level(m_log_level);


	falco_logger::set_sinsp_logging(
		config.get_scalar<bool>("libs_logger.enabled", false),
		config.get_scalar<std::string>("libs_logger.severity", "debug"),
		"[libs]: ");

	falco_logger::log_stderr = config.get_scalar<bool>("log_stderr", false);
	falco_logger::log_syslog = config.get_scalar<bool>("log_syslog", true);

	m_output_timeout = config.get_scalar<uint32_t>("output_timeout", 2000);

	std::string rule_matching = config.get_scalar<std::string>("rule_matching", "first");
	if (!falco_common::parse_rule_matching(rule_matching, m_rule_matching))
	{
		throw std::logic_error("Unknown rule matching strategy \"" + rule_matching + "\"--must be one of first, all");
	}

	std::string priority = config.get_scalar<std::string>("priority", "debug");
	if (!falco_common::parse_priority(priority, m_min_priority))
	{
		throw std::logic_error("Unknown priority \"" + priority + "\"--must be one of emergency, alert, critical, error, warning, notice, informational, debug");
	}

	m_buffered_outputs = config.get_scalar<bool>("buffered_outputs", false);
	m_outputs_queue_capacity = config.get_scalar<size_t>("outputs_queue.capacity", DEFAULT_OUTPUTS_QUEUE_CAPACITY_UNBOUNDED_MAX_LONG_VALUE);
	// We use 0 in falco.yaml to indicate an unbounded queue; equivalent to the largest long value 
	if (m_outputs_queue_capacity == 0)
	{
		m_outputs_queue_capacity = DEFAULT_OUTPUTS_QUEUE_CAPACITY_UNBOUNDED_MAX_LONG_VALUE;
	}

	m_time_format_iso_8601 = config.get_scalar<bool>("time_format_iso_8601", false);

	m_webserver_enabled = config.get_scalar<bool>("webserver.enabled", false);
	m_webserver_threadiness = config.get_scalar<uint32_t>("webserver.threadiness", 0);
	m_webserver_listen_port = config.get_scalar<uint32_t>("webserver.listen_port", 8765);
	m_webserver_listen_address = config.get_scalar<std::string>("webserver.listen_address", "0.0.0.0");
	if(!re2::RE2::FullMatch(m_webserver_listen_address, ip_address_re))
	{
		throw std::logic_error("Error reading config file (" + config_name + "): webserver listen address \"" + m_webserver_listen_address + "\" is not a valid IP address");
	}

	m_webserver_k8s_healthz_endpoint = config.get_scalar<std::string>("webserver.k8s_healthz_endpoint", "/healthz");
	m_webserver_ssl_enabled = config.get_scalar<bool>("webserver.ssl_enabled", false);
	m_webserver_ssl_certificate = config.get_scalar<std::string>("webserver.ssl_certificate", "/etc/falco/falco.pem");
	if(m_webserver_threadiness == 0)
	{
		m_webserver_threadiness = falco::utils::hardware_concurrency();
	}

	std::list<std::string> syscall_event_drop_acts;
	config.get_sequence(syscall_event_drop_acts, "syscall_event_drops.actions");

	m_syscall_evt_drop_actions.clear();
	for(const std::string &act : syscall_event_drop_acts)
	{
		if(act == "ignore")
		{
			m_syscall_evt_drop_actions.insert(syscall_evt_drop_action::DISREGARD);
		}
		else if(act == "log")
		{
			if(m_syscall_evt_drop_actions.count(syscall_evt_drop_action::DISREGARD))
			{
				throw std::logic_error("Error reading config file (" + config_name + "): syscall event drop action \"" + act + "\" does not make sense with the \"ignore\" action");
			}
			m_syscall_evt_drop_actions.insert(syscall_evt_drop_action::LOG);
		}
		else if(act == "alert")
		{
			if(m_syscall_evt_drop_actions.count(syscall_evt_drop_action::DISREGARD))
			{
				throw std::logic_error("Error reading config file (" + config_name + "): syscall event drop action \"" + act + "\" does not make sense with the \"ignore\" action");
			}
			m_syscall_evt_drop_actions.insert(syscall_evt_drop_action::ALERT);
		}
		else if(act == "exit")
		{
			m_syscall_evt_drop_actions.insert(syscall_evt_drop_action::EXIT);
		}
		else
		{
			throw std::logic_error("Error reading config file (" + config_name + "): available actions for syscall event drops are \"ignore\", \"log\", \"alert\", and \"exit\"");
		}
	}

	if(m_syscall_evt_drop_actions.empty())
	{
		m_syscall_evt_drop_actions.insert(syscall_evt_drop_action::DISREGARD);
	}

	m_syscall_evt_drop_threshold = config.get_scalar<double>("syscall_event_drops.threshold", .1);
	if(m_syscall_evt_drop_threshold < 0 || m_syscall_evt_drop_threshold > 1)
	{
		throw std::logic_error("Error reading config file (" + config_name + "): syscall event drops threshold must be a double in the range [0, 1]");
	}
	m_syscall_evt_drop_rate = config.get_scalar<double>("syscall_event_drops.rate", .03333);
	m_syscall_evt_drop_max_burst = config.get_scalar<double>("syscall_event_drops.max_burst", 1);
	m_syscall_evt_simulate_drops = config.get_scalar<bool>("syscall_event_drops.simulate_drops", false);

	m_syscall_evt_timeout_max_consecutives = config.get_scalar<uint32_t>("syscall_event_timeouts.max_consecutives", 1000);
	if(m_syscall_evt_timeout_max_consecutives == 0)
	{
		throw std::logic_error("Error reading config file(" + config_name + "): the maximum consecutive timeouts without an event must be an unsigned integer > 0");
	}

	m_falco_libs_thread_table_size = config.get_scalar<std::uint32_t>("falco_libs.thread_table_size", DEFAULT_FALCO_LIBS_THREAD_TABLE_SIZE);

	m_base_syscalls_custom_set.clear();
	config.get_sequence<std::unordered_set<std::string>>(m_base_syscalls_custom_set, std::string("base_syscalls.custom_set"));
	m_base_syscalls_repair = config.get_scalar<bool>("base_syscalls.repair", false);

	m_metrics_enabled = config.get_scalar<bool>("metrics.enabled", false);
	m_metrics_interval_str = config.get_scalar<std::string>("metrics.interval", "5000");
	m_metrics_interval = falco::utils::parse_prometheus_interval(m_metrics_interval_str);
	m_metrics_stats_rule_enabled = config.get_scalar<bool>("metrics.output_rule", false);
	m_metrics_output_file = config.get_scalar<std::string>("metrics.output_file", "");

	m_metrics_flags = 0;
	if (config.get_scalar<bool>("metrics.resource_utilization_enabled", true))
	{
		m_metrics_flags |= PPM_SCAP_STATS_RESOURCE_UTILIZATION;

	}
	if (config.get_scalar<bool>("metrics.state_counters_enabled", true))
	{
		m_metrics_flags |= PPM_SCAP_STATS_STATE_COUNTERS;

	}
	if (config.get_scalar<bool>("metrics.kernel_event_counters_enabled", true))
	{
		m_metrics_flags |= PPM_SCAP_STATS_KERNEL_COUNTERS;

	}
	if (config.get_scalar<bool>("metrics.libbpf_stats_enabled", true))
	{
		m_metrics_flags |= PPM_SCAP_STATS_LIBBPF_STATS;

	}

	m_metrics_convert_memory_to_mb = config.get_scalar<bool>("metrics.convert_memory_to_mb", true);
	m_metrics_include_empty_values = config.get_scalar<bool>("metrics.include_empty_values", false);

	std::vector<std::string> load_plugins;

	bool load_plugins_node_defined = config.is_defined("load_plugins");
	config.get_sequence<std::vector<std::string>>(load_plugins, "load_plugins");

	std::list<falco_configuration::plugin_config> plugins;
	try
	{
		if (config.is_defined("plugins"))
		{
			config.get_sequence<std::list<falco_configuration::plugin_config>>(plugins, std::string("plugins"));
		}
	}
	catch (std::exception &e)
	{
		// Might be thrown due to not being able to open files
		throw std::logic_error("Error reading config file(" + config_name + "): could not load plugins config: " + e.what());
	}

	// If load_plugins was specified, only save plugins matching those in values
	m_plugins.clear();
	if (!load_plugins_node_defined)
	{
		// If load_plugins was not specified at all, every plugin is added.
		// The loading order is the same as the sequence in the YAML config.
		m_plugins = { plugins.begin(), plugins.end() };
	}
	else
	{
		// If load_plugins is specified, only plugins contained in its list
		// are added, with the same order as in the list.
		for (const auto& pname : load_plugins)
		{
			bool found = false;
			for (const auto& p : plugins)
			{
				if (pname == p.m_name)
				{
					m_plugins.push_back(p);
					found = true;
					break;
				}
			}
			if (!found)
			{
				throw std::logic_error("Cannot load plugin '" + pname + "': plugin config not found for given name");
			}
		}
	}

	m_watch_config_files = config.get_scalar<bool>("watch_config_files", true);
}

void falco_configuration::read_rules_file_directory(const std::string &path, std::list<std::string> &rules_filenames, std::list<std::string> &rules_folders)
{
	fs::path rules_path = std::string(path);

	if(fs::is_directory(rules_path))
	{
		rules_folders.push_back(path);

		// It's a directory. Read the contents, sort
		// alphabetically, and add every path to
		// rules_filenames
		std::vector<std::string> dir_filenames;

		const auto it_options = fs::directory_options::follow_directory_symlink
											| fs::directory_options::skip_permission_denied;

		for (auto const& dir_entry : fs::directory_iterator(rules_path, it_options))
		{
			if(std::filesystem::is_regular_file(dir_entry.path()))
			{
				dir_filenames.push_back(dir_entry.path().string());
			}
		}

		std::sort(dir_filenames.begin(),
			  dir_filenames.end());

		for(std::string &ent : dir_filenames)
		{
			rules_filenames.push_back(ent);
		}
	}
	else
	{
		// Assume it's a file and just add to
		// rules_filenames. If it can't be opened/etc that
		// will be reported later..
		rules_filenames.push_back(path);
	}
}

static bool split(const std::string &str, char delim, std::pair<std::string, std::string> &parts)
{
	size_t pos;

	if((pos = str.find_first_of(delim)) == std::string::npos)
	{
		return false;
	}
	parts.first = str.substr(0, pos);
	parts.second = str.substr(pos + 1);

	return true;
}

void falco_configuration::init_cmdline_options(yaml_helper& config, const std::vector<std::string> &cmdline_options)
{
	for(const std::string &option : cmdline_options)
	{
		set_cmdline_option(config, option);
	}
}

void falco_configuration::set_cmdline_option(yaml_helper& config, const std::string &opt)
{
	std::pair<std::string, std::string> keyval;

	if(!split(opt, '=', keyval))
	{
		throw std::logic_error("Error parsing config option \"" + opt + "\". Must be of the form key=val or key.subkey=val");
	}

	config.set_scalar(keyval.first, keyval.second);
}
