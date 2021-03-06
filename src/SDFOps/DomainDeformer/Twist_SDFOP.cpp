
#include "Twist_SDFOP.h" 

static const std::vector<Argument> args = {
    {"a", ReturnType::Float, "0.0f"},
    {"b", ReturnType::Float, "0.0f"}
};

Twist_SDFOP::Twist_SDFOP() // :
   // BaseSDFOP::BaseSDFOP()
{
    m_returnType = ReturnType::Float;
} 
 
Twist_SDFOP::~Twist_SDFOP() 
{ 
} 


std::string Twist_SDFOP::getFunctionName()
{
    return "twist";
}

std::string Twist_SDFOP::getSource()
{
	return "return 0.0;";
}

Argument Twist_SDFOP::getArgument(unsigned int index)
{
    return args.at(index);
}

unsigned int Twist_SDFOP::argumentSize()
{
    return args.size();
}
