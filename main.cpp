#include "src/autoaligner.hpp"

//

int main (const int argc, const char** argv) {
	if (argc < 3) return 1;
	
	Optimiser optimiser { argv[1] };

	Optimiser::generateDwarfDump(argv[2], "../a.dump");
	optimiser.loadDwarfDump("../a.dump");
	optimiser.generateChanges();
	optimiser.applyChanges();

	return 0;
}
