/*
Copyright (C) 2022 The Falco Authors.

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

#include "actions.h"
#include <plugin_manager.h>

using namespace falco::app;
using namespace falco::app::actions;

falco::app::run_result falco::app::actions::list_plugins(falco::app::state& s)
{
	if(s.options.list_plugins)
	{
		std::ostringstream os;
		std::unique_ptr<sinsp> inspector(new sinsp());
		const auto& configs = s.config->m_plugins;
		for (auto &c : configs)
		{
			// load the plugin (no need to initialize it)
			auto plugin = inspector->register_plugin(c.m_library_path);
			format_plugin_info(plugin, os);
			os << std::endl;
		}

		printf("%lu Plugins Loaded:\n\n%s\n", configs.size(), os.str().c_str());
		return run_result::exit();
	}

	return run_result::ok();
}