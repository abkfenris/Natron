//  Natron
//
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
*Created by Alexandre GAUTHIER-FOICHAT on 6/1/2012. 
*contact: immarespond at gmail dot com
*
*/

#include "ExrEncoder.h"

#include <stdexcept>
#include <QtCore/QMutexLocker>
#include <QtCore/QMutex>
#include <ImfChannelList.h>
#include <ImfArray.h>
#include <ImfOutputFile.h>
#include <half.h>

#include "Global/AppManager.h"

#include "Engine/Lut.h"
#include "Engine/Row.h"
#include "Engine/KnobTypes.h"
#include "Engine/KnobFactory.h"

#include "Writers/Writer.h"

using std::make_pair;
using std::cout; using std::endl;

#ifndef OPENEXR_IMF_NAMESPACE
#define OPENEXR_IMF_NAMESPACE Imf
#endif
namespace Imf_ = OPENEXR_IMF_NAMESPACE;

struct ExrEncoder::Implementation {
    Implementation();
    Imf_::OutputFile* outputFile;
    int depth;
    Natron::ChannelSet channels;
    Imath::Box2i exrDataW;
    Imath::Box2i exrDispW;
    QMutex lock;
};

namespace EXR {
static std::string const compressionNames[6]={
    "No compression",
    "Zip (1 scanline)",
    "Zip (16 scanlines)",
    "PIZ Wavelet (32 scanlines)",
    "RLE",
    "B44"
};

static Imf_::Compression stringToCompression(const std::string& str){
    if(str == compressionNames[0]){
        return Imf_::NO_COMPRESSION;
    }else if(str == compressionNames[1]){
        return Imf_::ZIPS_COMPRESSION;
    }else if(str == compressionNames[2]){
        return Imf_::ZIP_COMPRESSION;
    }else if(str == compressionNames[3]){
        return Imf_::PIZ_COMPRESSION;
    }else if(str == compressionNames[4]){
        return Imf_::RLE_COMPRESSION;
    }else{
        return Imf_::B44_COMPRESSION;
    }
}

static  std::string const depthNames[2] = {
    "16 bit half", "32 bit float"
};

static int depthNameToInt(const std::string& name){
    if(name == depthNames[0]){
        return 16;
    }else{
        return 32;
    }
}

static std::string toExrChannel(Natron::Channel from) {
    switch (from) {
        case Natron::Channel_red:
            return "R";
        case Natron::Channel_green:
            return "G";
        case Natron::Channel_blue:
            return "B";
        case Natron::Channel_alpha:
            return "A";
        case Natron::Channel_Z:
            return "Z";
        default:
            return Natron::getChannelName(from);
    }
}

#if 0 // unused functions
static bool timeCodeFromString(const std::string& str, Imf_::TimeCode& attr)
{
    if (str.length() != 11)
        return false;

    int hours = 0, mins = 0, secs = 0, frames = 0;

    sscanf(str.c_str(), "%02d:%02d:%02d:%02d", &hours, &mins, &secs, &frames);
    try {
        attr.setHours(hours);
        attr.setMinutes(mins);
        attr.setSeconds(secs);
        attr.setFrame(frames);
    } catch (const std::exception& e) {
        qDebug() << "EXR: Time Code Metadata warning" << ": " <<  e.what();
        return false;
    } catch (...) {
        qDebug() << "EXR: Time Code Metadata warning";
        return false;
    }
    return true;
}

static bool edgeCodeFromString(const std::string& str, Imf_::KeyCode& a)
{
    int mfcCode, filmType, prefix, count, perfOffset;
    sscanf(str.c_str(), "%d %d %d %d %d", &mfcCode, &filmType, &prefix, &count, &perfOffset);

    try {
        a.setFilmMfcCode(mfcCode);
        a.setFilmType(filmType);
        a.setPrefix(prefix);
        a.setCount(count);
        a.setPerfOffset(perfOffset);
    } catch (const std::exception& e) {
        qDebug() << "EXR: Edge Code Metadata warning" << ": " <<  e.what();
        return false;
    } catch (...) {
        qDebug() << "EXR: Edge Code Metadata warning";
        return false;
    }

    return true;
}
#endif //0
} // namespace EXR

ExrEncoder::ExrEncoder(Writer* writer)
: Encoder(writer)
, _imp(new Implementation)
{
}

ExrEncoder::Implementation::Implementation()
: outputFile(NULL)
,depth(0)
,channels()
,lock()
{
}

ExrEncoder::~ExrEncoder(){

}

std::vector<std::string> ExrEncoder::fileTypesEncoded() const {
    std::vector<std::string> out;
    out.push_back("exr");
    return out;
}

void ExrEncoderKnobs::initKnobs(const std::string& fileType) {
    std::string separatorDesc(fileType);
    separatorDesc.append(" Options");
    sepKnob = appPTR->getKnobFactory().createKnob<Separator_Knob>(_writer, separatorDesc);
    
    std::string compressionCBDesc("Compression");
    compressionCBKnob = appPTR->getKnobFactory().createKnob<Choice_Knob>(_writer, compressionCBDesc);
    std::vector<std::string> list;
    for (int i =0; i < 6; ++i) {
        list.push_back(EXR::compressionNames[i].c_str());
    }
    compressionCBKnob->populate(list);
    compressionCBKnob->setValue(3);
    
    std::string depthCBDesc("Data type");
    depthCBKnob = appPTR->getKnobFactory().createKnob<Choice_Knob>(_writer,depthCBDesc);
    list.clear();
    for(int i = 0 ; i < 2 ; ++i) {
        list.push_back(EXR::depthNames[i].c_str());
    }
    depthCBKnob->populate(list);
    depthCBKnob->setValue(1);
    
    /*calling base-class version at the end*/
    EncoderKnobs::initKnobs(fileType);
}
void ExrEncoderKnobs::cleanUpKnobs(){
    sepKnob.reset();
    compressionCBKnob.reset();
    depthCBKnob.reset();
}

bool ExrEncoderKnobs::allValid(){
    return true;
}

/*Must implement it to initialize the appropriate colorspace  for
 the file type. You can initialize the _lut member by calling the
 function getLut(datatype) */
void ExrEncoder::initializeColorSpace(){
    _lut = Natron::Color::getLut(Natron::Color::LUT_DEFAULT_FLOAT);
}

/*This must be implemented to do the output colorspace conversion*/
Natron::Status ExrEncoder::render(boost::shared_ptr<const Natron::Image> inputImage,int /*view*/,const RectI& roi){
    
    try {
        for (int y = roi.bottom(); y < roi.top(); ++y) {
            if(_writer->aborted()){
                return Natron::StatFailed;
            }
            /*First we create a row that will serve as the output buffer.
             We copy the scan-line (with y inverted) in the inputImage to the row.*/
            int exrY = roi.top() - y - 1;

            Natron::Row row(roi.left(),y,roi.right(),Natron::Mask_RGBA);
            const float* src_pixels = inputImage->pixelAt(roi.left(), exrY);
            if(exrY < inputImage->getRoD().height()){
                foreachChannels(z, _imp->channels){
                    float* to = row.begin(z);
                    for (int i = 0; i < roi.width(); ++i) {
                        if( i < inputImage->getRoD().width()){
                            to[i] = src_pixels[i*4 + z -1];
                        }else{
                            to[i] = 0.f;
                        }
                    }
                    //colorspace conversion
                    to_float(z, to, to, row.begin(Natron::Channel_alpha), row.width());
                }
            }else{
                row.eraseAll();
            }
            
            if(_writer->aborted()){
                return Natron::StatFailed;
            }
            
            /*we create the frame buffer*/
            Imf_::FrameBuffer fbuf;
            Imf_::Array2D<half>* halfwriterow = 0 ;
            if ( _imp->depth == 32) {
                foreachChannels(z, _imp->channels){
                  
                    std::string channame = EXR::toExrChannel(z);
                    fbuf.insert(channame.c_str(),
                                Imf_::Slice(Imf_::FLOAT, (char*)row.begin(z),
                                            sizeof(float), 0));
                }
            } else {
                halfwriterow = new Imf_::Array2D<half>(_imp->channels.size() ,roi.width());
                
                int channelCount = 0;
                foreachChannels(z, _imp->channels){
                    std::string channame = EXR::toExrChannel(z);
                    fbuf.insert(channame.c_str(),
                                Imf_::Slice(Imf_::HALF,
                                            (char*)(&(*halfwriterow)[channelCount][0] - _imp->exrDataW.min.x),
                                            sizeof((*halfwriterow)[channelCount][0]), 0));
                    const float* from = row.begin(z);
                    for (int i = _imp->exrDataW.min.x; i < _imp->exrDataW.max.x ; ++i) {
                        (*halfwriterow)[channelCount][i - _imp->exrDataW.min.x] = from[i];
                    }
                    ++channelCount;
                }
                delete halfwriterow;
            }
            QMutexLocker locker(&_imp->lock);
            _imp->outputFile->setFrameBuffer(fbuf);
            _imp->outputFile->writePixels(1);
        }
    } catch (const std::exception& e) {
        _writer->setPersistentMessage(Natron::ERROR_MESSAGE, std::string("OpenEXR error") + ": " + e.what());
        return Natron::StatFailed;
    } catch (...) {
        _writer->setPersistentMessage(Natron::ERROR_MESSAGE, std::string("OpenEXR error"));
        return Natron::StatFailed;
    }
    return Natron::StatOK;

}

/*This function initialises the output file/output storage structure and put necessary info in it, like
 meta-data, channels, etc...This is called on the main thread so don't do any extra processing here,
 otherwise it would stall the GUI.*/
Natron::Status ExrEncoder::setupFile(const QString& filename, const RectI& rod) {
    try {
        ExrEncoderKnobs* knobs = dynamic_cast<ExrEncoderKnobs*>(_optionalKnobs);
        Imf_::Compression compression(EXR::stringToCompression(knobs->_compression));
        _imp->depth = EXR::depthNameToInt(knobs->_dataType);
        Imath::Box2i exrDataW;
        exrDataW.min.x = rod.left();
        exrDataW.min.y = rod.height() - rod.top();
        exrDataW.max.x = rod.right() - 1;
        exrDataW.max.y = rod.height() - rod.bottom() - 1;
        
        Imath::Box2i exrDispW;
        exrDispW.min.x = 0;
        exrDispW.min.y = 0;
        exrDispW.max.x = rod.width() - 1;
        exrDispW.max.y = rod.height() - 1;
        _imp->channels  = _writer->requestedChannels();
        Imf_::Header exrheader(exrDispW, exrDataW, 1.,
                               Imath::V2f(0, 0), 1, Imf_::INCREASING_Y, compression);
        
        foreachChannels(z, _imp->channels){
            std::string channame = EXR::toExrChannel(z);
            if (_imp->depth == 32) {
                exrheader.channels().insert(channame.c_str(), Imf_::Channel(Imf_::FLOAT));
            } else {
                assert(_imp->depth == 16);
                exrheader.channels().insert(channame.c_str(), Imf_::Channel(Imf_::HALF));
            }
        }
        
        _imp->outputFile = new Imf_::OutputFile(filename.toStdString().c_str(),exrheader);
        _imp->exrDataW = exrDataW;
        _imp->exrDispW = exrDispW;
    } catch (const std::exception& e) {
        qDebug() << "OpenEXR error" << ": " << e.what();
        return Natron::StatFailed;
    } catch (...) {
        qDebug() << "OpenEXR error";
        return Natron::StatFailed;
    }
    return Natron::StatOK;
}
