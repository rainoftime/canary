/*
 * Developed by Qingkai Shi
 * Copy Right by Prism Research Group, HKUST and State Key Lab for Novel Software Tech., Nanjing University.  
 */

#include "DyckCG/DyckCallGraph.h"

static cl::opt<bool>
WithEdgeLabels("with-labels", cl::init(false), cl::Hidden,
        cl::desc("Determine whether there are edge lables in the cg."));

void DyckCallGraph::dotCallGraph(const string& mIdentifier) {
    string dotfilename("");
    dotfilename.append(mIdentifier);
    dotfilename.append(".maycg.dot");

    FILE * fout = fopen(dotfilename.data(), "w+");
    fprintf(fout, "digraph maycg {\n");
    
    auto fwIt = FunctionMap.begin();
    while (fwIt != FunctionMap.end()) {
        DyckCallGraphNode* fw = fwIt->second;
        fprintf(fout, "\tf%d[label=\"%s\"]\n", fw->getIndex(), fw->getLLVMFunction()->getName().data());
        fwIt++;
    }

    fwIt = FunctionMap.begin();
    while (fwIt != FunctionMap.end()) {
        DyckCallGraphNode* fw = fwIt->second;
        set<CommonCall*>* commonCalls = &(fw->getCommonCalls());
        set<CommonCall*>::iterator comIt = commonCalls->begin();
        while (comIt != commonCalls->end()) {
            CommonCall* cc = *comIt;
            Function * callee = (Function*) cc->calledValue;

            if (FunctionMap.count(callee)) {
                if (WithEdgeLabels) {
                    Value * ci = cc->instruction;
                    std::string s;
                    raw_string_ostream rso(s);
                    if (ci != NULL) {
                        rso << *(ci);
                    } else {
                        rso << "Hidden";
                    }
                    string& edgelabel = rso.str();
                    for (unsigned int i = 0; i < edgelabel.length(); i++) {
                        if (edgelabel[i] == '\"') {
                            edgelabel[i] = '`';
                        }

                        if (edgelabel[i] == '\n') {
                            edgelabel[i] = ' ';
                        }
                    }
                    fprintf(fout, "\tf%d->f%d[label=\"%s\"]\n", fw->getIndex(), FunctionMap[callee]->getIndex(), edgelabel.data());
                } else {
                    fprintf(fout, "\tf%d->f%d\n", fw->getIndex(), FunctionMap[callee]->getIndex());
                }
            } else {
                errs() << "ERROR in printCG when print common function calls.\n";
                exit(-1);
            }
            comIt++;
        }

        set<PointerCall*>* fpCallsMap = &(fw->getPointerCalls());
        set<PointerCall*>::iterator fpIt = fpCallsMap->begin();
        while (fpIt != fpCallsMap->end()) {
            PointerCall* pcall = *fpIt;
            set<Function*>* mayCalled = &((*fpIt)->mayAliasedCallees);

            char * edgeLabelData = NULL;
            if (WithEdgeLabels) {
                std::string s;
                raw_string_ostream rso(s);
                if (pcall->instruction != NULL) {
                    rso << *(pcall->instruction);
                } else {
                    rso << "Hidden";
                }
                string& edgelabel = rso.str(); // edge label is the call inst
                for (unsigned int i = 0; i < edgelabel.length(); i++) {
                    if (edgelabel[i] == '\"') {
                        edgelabel[i] = '`';
                    }

                    if (edgelabel[i] == '\n') {
                        edgelabel[i] = ' ';
                    }
                }
                edgeLabelData = const_cast<char*> (edgelabel.data());
            }
            set<Function*>::iterator mcIt = mayCalled->begin();
            while (mcIt != mayCalled->end()) {
                Function * mcf = *mcIt;
                if (FunctionMap.count(mcf)) {
                    if (WithEdgeLabels) {
                        fprintf(fout, "\tf%d->f%d[label=\"%s\"]\n", fw->getIndex(), FunctionMap[mcf]->getIndex(), edgeLabelData);
                    } else {
                        fprintf(fout, "\tf%d->f%d\n", fw->getIndex(), FunctionMap[mcf]->getIndex());
                    }
                } else {
                    errs() << "ERROR in printCG when print fp calls.\n";
                    exit(-1);
                }

                mcIt++;
            }

            fpIt++;
        }


        fwIt++;
    }

    fprintf(fout, "}\n");
    fclose(fout);
}

void DyckCallGraph::printFunctionPointersInformation(const string& mIdentifier) {
    string dotfilename("");
    dotfilename.append(mIdentifier);
    dotfilename.append(".fp.txt");

    FILE * fout = fopen(dotfilename.data(), "w+");

    auto fwIt = this->begin();
    while (fwIt != this->end()) {
        DyckCallGraphNode* fw = fwIt->second;

        set<PointerCall*>* fpCallsMap = &(fw->getPointerCalls());
        set<PointerCall*>::iterator fpIt = fpCallsMap->begin();
        while (fpIt != fpCallsMap->end()) {
            /*Value * callInst = fpIt->first;
            std::string s;
            raw_string_ostream rso(s);
            rso << *(callInst);
            string& edgelabel = rso.str();
            for (unsigned int i = 0; i < edgelabel.length(); i++) {
                if (edgelabel[i] == '\"') {
                    edgelabel[i] = '`';
                }

                if (edgelabel[i] == '\n') {
                    edgelabel[i] = ' ';
                }
            }
            fprintf(fout, "CallInst: %s\n", edgelabel.data()); //call inst
             */
            set<Function*>* mayCalled = &((*(fpIt))->mayAliasedCallees);
            fprintf(fout, "%zd\n", mayCalled->size()); //number of functions

            // what functions?
            set<Function*>::iterator mcIt = mayCalled->begin();
            while (mcIt != mayCalled->end()) {
                // Function * mcf = *mcIt;
                //fprintf(fout, "%s\n", mcf->getName().data());

                mcIt++;
            }

            //fprintf(fout, "\n");

            fpIt++;
        }


        fwIt++;
    }

    fclose(fout);
}

void DyckCallGraph::printFunctionPointerStat() {
    int CG_BUCKET_NUMBER = 11;
    int cg_bucket[CG_BUCKET_NUMBER] = { 0 };
    int cg_bucket_steps[CG_BUCKET_NUMBER] = { 0, 1, 2, 3, 4, 5, 6, 7, 10, 30, 100 };
    auto fwIt = this->begin();
    while (fwIt != this->end()) {
        DyckCallGraphNode* fw = fwIt->second;

        set<PointerCall*>* fpCallsMap = &(fw->getPointerCalls());
        set<PointerCall*>::iterator fpIt = fpCallsMap->begin();
        while (fpIt != fpCallsMap->end()) {
            set<Function*>* mayCalled = &((*(fpIt))->mayAliasedCallees);
            int i;
            for (i = 0; i < CG_BUCKET_NUMBER - 1; i++) {
                if (mayCalled->size() < cg_bucket_steps[i + 1]) {
                    cg_bucket[i]++;
                    break;
                }
            }

            // what functions?
            set<Function*>::iterator mcIt = mayCalled->begin();
            while (mcIt != mayCalled->end()) {
                // Function * mcf = *mcIt;
                //fprintf(fout, "%s\n", mcf->getName().data());

                mcIt++;
            }
            fpIt++;
        }
        fwIt++;
    }
    outs() << "============Canary CG Resolution Statistics Begin===============\n";
    outs() << "\n";
    int i;
    for (i = 0; i < CG_BUCKET_NUMBER - 1; i++) {
        if (cg_bucket_steps[i] == cg_bucket_steps[i + 1] - 1)
            outs() << "\t" << cg_bucket_steps[i] << ":\t\t";
        else
            outs() << "\t" << cg_bucket_steps[i] << " - " << cg_bucket_steps[i + 1] - 1 << ": \t\t";
        outs() << cg_bucket[i] << "\n";
    }
    outs() << "\t>" << cg_bucket_steps[i] << ": \t\t";
    outs() << cg_bucket[i] << "\n";
    outs() << "\n";
    outs() << "============Canary CG Resolution Statistics End===============\n";
}


