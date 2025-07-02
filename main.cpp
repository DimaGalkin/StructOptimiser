#include "src/autoaligner.hpp"

//

int main (const int argc, const char** argv) {
	Optimiser optimiser { "/home/dima/Projects" };

	Optimiser::generateDwarfDump("../sao", "../a.dump");
	optimiser.loadDwarfDump("../a.dump");
	optimiser.generateChanges();
	optimiser.applyChanges();

	return 0;
}