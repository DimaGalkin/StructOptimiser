#include "autoaligner.hpp"

#include <filesystem>
#include <flat_set>
#include <sstream>
#include <fstream>
#include <string_view>

namespace {
	const std::flat_set<std::string> TYPE_TAGS {
		"DW_TAG_class_type",
		"DW_TAG_array_type",
		"DW_TAG_base_type",
		"DW_TAG_const_type",
		"DW_TAG_enumeration_type",
		"DW_TAG_file_type",
		"DW_TAG_interface_type",
		"DW_TAG_packed_type",
		"DW_TAG_pointer_type",
		"DW_TAG_ptr_to_member_type",
		"DW_TAG_reference_type",
		"DW_TAG_set_type",
		"DW_TAG_shared_type",
		"DW_TAG_string_type",
		"DW_TAG_structure_type",
		"DW_TAG_subrange_type",
		"DW_TAG_subroutine_type",
		"DW_TAG_thrown_type",
		"DW_TAG_union_type",
		"DW_TAG_unspecified_type",
		"DW_TAG_volatile_type",
		"DW_TAG_typedef"
	};
}

std::string reduceSpaces (
	const std::string_view str
) {
	std::ostringstream out;
	bool inSpace = false;

	for (char ch : str) {
		if (std::isspace(static_cast<unsigned char>(ch))) {
			if (!inSpace) {
				out << ' ';
				inSpace = true;
			}
		} else {
			out << ch;
			inSpace = false;
		}
	}

	std::string result = out.str();
	if (!result.empty() && result[0] == ' ')
		result.erase(0, 1);

	return result;
}

size_t Optimiser::collapseTypeSize (
	const unsigned long long loc
) {
	if (!type_map_.contains(loc)) throw std::runtime_error(std::to_string(loc));
	const auto [at_type, byte_size] = type_map_.at(loc);
	if (byte_size.has_value()) return byte_size.value();

	return collapseTypeSize(at_type);
}

void Optimiser::loadDwarfDump (
	const std::string_view filename
) {
	std::ifstream dwarfdump { filename.data() };

	std::string line;

	while (std::getline(dwarfdump, line))
	{
		line = reduceSpaces(line);
		if (line.empty()) continue;

		if (line.starts_with("0x")) {
			parseAddressLine(line);
			continue;
		}

		const size_t space_loc = line.find(' ');
		const std::string tag = line.substr(0, space_loc);
		const std::string data = line.substr(space_loc + 1);

		if ( processing_parent_ )
		{
			parseParentTagLine(tag, data);
		}
		if ( processing_child_ )
		{
			parseChildTagLine(tag, data);
		}
		if ( processing_type_ )
		{
			parseTypeTagLine(tag, data);
		}
	}
}

void Optimiser::parseAddressLine (
	const std::string_view line
) {
	if ( processing_namespace_ && !current_namespace_.decl_file.empty() )
	{
		namespaces_.push_back(current_namespace_);
		processing_namespace_ = false;
	}

	if ( processing_parent_ && !current_parent_.decl_file.empty())
	{
		parents_.push_back(current_parent_);
		processing_parent_ = false;
	}

	if ( processing_child_ )
	{
		children_.push_back(current_child_);
		processing_child_ = false;
	}

	if ( processing_type_ )
	{
		type_map_.insert(std::make_pair(current_type_address_, current_type_));
		processing_type_ = false;
	}

	current_namespace_ = {};
	current_parent_ = {};
	current_child_ = {};
	current_type_ = {};

	const size_t space_loc = line.find(' ');
	const auto tag = line.substr(space_loc + 1);
	const auto address = line.substr(0, space_loc - 1); // -1 to remove colon

	if (TYPE_TAGS.contains(tag.data()))
	{
		processing_type_ = true;
		current_type_address_ = strtoll(address.data(), nullptr, 16);

		if (
			tag == "DW_TAG_structure_type" ||
			tag == "DW_TAG_union_type"
		) {
			processing_parent_ = true;
			current_parent_.start = current_type_address_;
		}
		else if ( tag == "DW_TAG_class_type" )
		{
			processing_parent_ = true;
			current_parent_.start = current_type_address_;
			current_parent_.is_class = true;
		}
	}
	else if (tag == "DW_TAG_member")
	{
		processing_child_ = true;
		current_child_.loc = strtoll(address.data(), nullptr, 16);
	}
	else if (tag == "DW_TAG_namespace")
	{
		processing_namespace_ = true;
		current_namespace_.start = strtoll(address.data(), nullptr, 16);
	}
}

void Optimiser::parseNamespaceTagLine (
	const std::string_view tag,
	const std::string_view data
) {
	if (tag == "DW_AT_name") {
		current_namespace_.name = data.substr(2, data.size() - 4);
	}
	else if (tag == "DW_AT_decl_file") {
		current_namespace_.decl_file = data.substr(2, data.size() - 4);

		if (!current_namespace_.decl_file.starts_with(path_to_optimise_) || current_namespace_.decl_file.contains("<built-in>")) {
			processing_namespace_ = false;
		}
	}
	else if (tag == "DW_AT_sibling") {
		current_namespace_.end = strtoll(data.substr(1, data.size() - 2).data(), nullptr, 16);
	}
}


void Optimiser::parseParentTagLine (
	const std::string_view tag,
	const std::string_view data
) {
	if (tag == "DW_AT_name") {
		current_parent_.name = data.substr(2, data.size() - 4);
	}
	else if (tag == "DW_AT_decl_file") {
		current_parent_.decl_file = data.substr(2, data.size() - 4);

		if (!current_parent_.decl_file.starts_with(path_to_optimise_) || current_parent_.decl_file.contains("<built-in>")) {
			processing_parent_ = false;
		}
	}
	else if (tag == "DW_AT_sibling") {
		current_parent_.end = strtoll(data.substr(1, data.size() - 2).data(), nullptr, 16);
	}
}

void Optimiser::parseChildTagLine (
	const std::string_view tag,
	const std::string_view data
) {
	if (tag == "DW_AT_name") {
		current_child_.name = data.substr(2, data.size() - 4);
	}
	else if (tag == "DW_AT_decl_file") {
		current_child_.decl_file = data.substr(2, data.size() - 4);

		if (!current_child_.decl_file.starts_with(path_to_optimise_) || current_child_.decl_file.contains("<built-in>")) {
			processing_child_ = false;
		}
	}
	else if (tag == "DW_AT_type") {
		current_child_.at_type = strtoll(data.substr(1, data.find(' ')).data(), nullptr, 16);
	}
	else if (tag == "DW_AT_accessibility") {
		current_child_.access = data.substr(1, data.size() - 2);
	}
}

void Optimiser::parseTypeTagLine (
	const std::string_view tag,
	const std::string_view data
) {
	if (tag == "DW_AT_byte_size") {
		current_type_.byte_size = strtoll(data.substr(1, data.size() - 2).data(), nullptr, 16);
	}
	else if (tag == "DW_AT_type") {
		current_type_.at_type = strtoll(data.substr(1, data.find(' ')).data(), nullptr, 16);
	}
}