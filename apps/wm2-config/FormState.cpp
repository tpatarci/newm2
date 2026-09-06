#include "FormState.h"

// RED-phase skeleton (plan 09-06, task 1). Every entry point exists with the
// signature the header declares so the display-free cases COMPILE and fail on
// their assertions rather than on a missing symbol -- a link error is not a red
// test, it is an absent one.

ConfigLayers configLayersFromDisk()
{
    return ConfigLayers();
}

void FormState::seedFromLayers(const ConfigLayers&)
{
}

void FormState::adoptEffective(const std::string&, const std::string&,
                               ValueSource, const std::string&)
{
}

bool FormState::manages(const std::string&) const
{
    return false;
}

const FormField* FormState::field(const std::string&) const
{
    return nullptr;
}

std::string FormState::value(const std::string&) const
{
    return std::string();
}

bool FormState::setValue(const std::string&, const std::string&)
{
    return false;
}

bool FormState::requestReset(const std::string&)
{
    return false;
}

bool FormState::dirty() const
{
    return false;
}

std::vector<ConfigEdit> FormState::edits() const
{
    return std::vector<ConfigEdit>();
}

void FormState::markSaved()
{
}

void FormState::revert()
{
}

std::vector<std::string> FormState::divergentKeys() const
{
    return std::vector<std::string>();
}

FormField* FormState::mutableField(const std::string&)
{
    return nullptr;
}

std::string formValueForKey(const Config&, const std::string&)
{
    return std::string();
}
