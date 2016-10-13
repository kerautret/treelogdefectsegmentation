#include <iostream>
#include <fstream>
#include <utility>

#include "DGtal/base/Common.h"
#include "DGtal/helpers/StdDefs.h"
#include "DGtal/io/writers/MeshWriter.h"
#include "DGtal/io/readers/PointListReader.h"



#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "DGtal/io/writers/MeshWriter.h"
#include "DGtal/io/readers/MeshReader.h"
#include "DGtal/shapes/Mesh.h"

#include "DGtal/io/colormaps/GradientColorMap.h"
#include "DGtal/io/colormaps/HueShadeColorMap.h"

#include "DefectSegmentation.h"
#include "IOHelper.h"
#include "Centerline/Centerline.h"
#include "Centerline/CenterlineHelper.h"


using namespace DGtal;
namespace po = boost::program_options;

typedef typename Mesh<Z3i::RealPoint>::MeshFace Face;



int
main(int argc,char **argv)
{
    po::options_description general_opt("Allowed options are: ");
    general_opt.add_options()
        ("help,h", "display this message")
        ("input,i", po::value<std::string>(), "input mesh.")
        ("accRadius,r", po::value<double>(), "accumulation radius.")
        ("trackStep,s", po::value<double>(), "tracking step.")
        ("invertNormal,n", "invert normal to apply accumulation.")
        ("binWidth,b", po::value<double>()->default_value(5.0), "bin width used to compute threshold")
        ("patchWidth,a", po::value<double>()->default_value(25), "Arc length/ width of patch")
        ("patchHeight,e", po::value<int>()->default_value(100), "Height of patch")
		("voxelSize", po::value<int>()->default_value(1), "Voxel size")
        ("output,o", po::value<std::string>()->default_value("output"), "output prefix: output-defect.off, output-def-faces-ids, ...");

    bool parseOK=true;
    po::variables_map vm;
    try{
        po::store(po::parse_command_line(argc, argv, general_opt), vm);
    }catch(const std::exception& ex){
        trace.info()<< "Error checking program options: "<< ex.what()<< std::endl;
        parseOK=false;
	}

	po::notify(vm);
	if(vm.count("help") || argc<=1 || !parseOK || !vm.count("input") || !vm.count("accRadius") || !vm.count("trackStep")){
		if(!vm.count("input")){
			trace.error()<<"the input mesh is required!"<<std::endl;
		}else if( !vm.count("accRadius") ){
			trace.error()<<"the accRadius is required!"<<std::endl;
		}else if( !vm.count("trackStep") ){
			trace.error()<<"the trackStep is required!"<<std::endl;
		}
		trace.info()<< "Segmentation log defects" <<std::endl << "Options: "<<std::endl
			<< general_opt << "\n";
		return 0;
	}


	int voxelSize = vm["voxelSize"].as<int>();
	assert(voxelSize > 0);

    DGtal::Mesh<Z3i::RealPoint> scaledMesh(true);

	double accRadius = vm["accRadius"].as<double>() / voxelSize;
	double trackStep = vm["trackStep"].as<double>() / voxelSize;
	bool invertNormal = vm.count("invertNormal");

	double binWidth = vm["binWidth"].as<double>();

    DGtal::Mesh<Z3i::RealPoint> oriMesh(true);
    std::string inputMeshName = vm["input"].as<std::string>();
    MeshReader<Z3i::RealPoint>::importOFFFile(inputMeshName, scaledMesh, false);
    MeshReader<Z3i::RealPoint>::importOFFFile(inputMeshName, oriMesh, false);
    std::vector<Z3i::RealPoint> pointCloud(scaledMesh.nbVertex());

	//adjust by voxelSize
	for ( int i = 0; i < scaledMesh.nbVertex(); i++ ){
		Z3i::RealPoint &p = scaledMesh.getVertex(i);
		p /= voxelSize;
	}

trace.info()<<"Begin accumulation:"<<std::endl;
trace.info()<<"trackStep:"<<trackStep<<std::endl;
trace.info()<<"accRadius:"<<accRadius<<std::endl;
	Centerline acc(scaledMesh, accRadius, trackStep, invertNormal);

    std::copy(oriMesh.vertexBegin(), oriMesh.vertexEnd(), pointCloud.begin());


    //@TODO:check input mesh and fiber here
    std::vector<Z3i::RealPoint> fiber = acc.compute();
	//unscale fiber for more accuracy Splines
	for(unsigned int i = 0; i < fiber.size(); i++){
		fiber[i] = fiber[i]*voxelSize;
	}

	std::pair<DGtal::Z3i::RealPoint, DGtal::Z3i::RealPoint> boudingBox = oriMesh.getBoundingBox();
	Z3i::RealPoint ptLow = boudingBox.first;
	Z3i::RealPoint ptUp = boudingBox.second;
	Z3i::Domain domain = Z3i::Domain(Z3i::Point((int) ptLow[0], (int) ptLow[1], (int) ptLow[2]),
						 Z3i::Point((int) ptUp[0], (int) ptUp[1], (int) ptUp[2]));

	std::vector<Z3i::RealPoint> centerline = CenterlineHelper::getSmoothCenterlineBSplines(domain, fiber);

    double patchWidth = vm["patchWidth"].as<double>(); 
    int patchHeight = vm["patchHeight"].as<int>(); 

trace.info()<<"arc:"<<patchWidth<<std::endl;
trace.info()<<"fiber:"<<centerline.size()<<std::endl;
trace.info()<<"pointcloud:"<<pointCloud.size()<<std::endl;

    std::vector<unsigned int> noDefectCloudIndices;
    DefectSegmentation sa(pointCloud, centerline, patchWidth, patchHeight, binWidth);
    sa.init();
    std::vector<unsigned int> defects = sa.getDefect();
    std::vector<double> distances = sa.getDistances();
    //build error map
    double minValue = 0.0;
    double maxValue = 10.0;
    DGtal::GradientColorMap<double, CMAP_JET>  gradientShade(minValue, maxValue); 
    for (unsigned int i = 0; i < scaledMesh.nbFaces(); i++){
        Face aFace = scaledMesh.getFace(i);
        //centroid
        double err = 0.0;
        for (unsigned int k = 0; k < aFace.size(); k++){
            err += distances.at(aFace.at(k));
        }   
        err /= aFace.size();
        if (err < minValue){
            err = minValue;
        }
        if(err > maxValue){
            err = maxValue;
        }
        scaledMesh.setFaceColor(i, gradientShade(err));
    }

    std::vector<bool> defectFlags(pointCloud.size(), false);
    for(unsigned int i = 0; i< defects.size(); i++){
        defectFlags[defects.at(i)] = true;
    }

    /*
    std::vector<bool> defectFlags = vote(pointCloud, defFlags);
    std::vector<unsigned int > defects;
    for(unsigned int i = 0; i < defectFlags.size(); i++){
        if(defectFlags[i]){
            defects.push_back(i);
        }
    }
    */
    
    std::vector<unsigned int> facesToDelete;
    //color defect mesh
    for (unsigned int i = 0; i < oriMesh.nbFaces(); i++){
        Face aFace = oriMesh.getFace(i);
        unsigned int c = 0;
        for (unsigned int k = 0; k < aFace.size(); k++){
            if(defectFlags.at(aFace.at(k))){
                c++;
            }
        }   
        if(c >= aFace.size()){
            oriMesh.setFaceColor(i, DGtal::Color::Green);
            facesToDelete.push_back(i);
        }
    }


    //write output mesh
    std::string outputPrefix = vm["output"].as<std::string>();
    std::string errorFile = outputPrefix + "-error.off";
    IOHelper::export2OFF(scaledMesh,errorFile);

    std::string defectFile = outputPrefix + "-defect.off";
    IOHelper::export2OFF(oriMesh,defectFile);

    //write defect id
    IOHelper::export2Text(defects, outputPrefix + "-defect.id");
    IOHelper::export2Text(facesToDelete, outputPrefix + "-def-faces.id");

trace.info()<<"finished"<<std::endl;
    return 0;
}
