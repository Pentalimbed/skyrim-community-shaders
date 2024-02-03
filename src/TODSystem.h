#pragma once

struct TODProfile
{
	struct Node
	{
		float time = .5;
		float value = 0;

		auto operator<=>(const Node&) const = default;
	};

	std::vector<Node> nodes = {};
	std::vector<size_t> sorted_indices = {};
	size_t cached_query = 0;

	float query(float t, bool cache = true);  // non-cached query used only by imgui
	void drawEditor(std::optional<float> t = std::nullopt);

	void rerank();
};

struct TODParameter
{
	std::string feature_name;
	std::string parameter_name;
};

struct TODCollection
{
	std::unordered_map<TODParameter, TODProfile> parameterConfigs;

	json query(float t);
};