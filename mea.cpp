/*
Copyright (C) 2014 Georgia Institute of Technology

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/*
* This module displays a raster plot of MEA activity
*/

#include "mea.h"
#include <iostream>
#include <QtGui>
#include <qwt_plot_renderer.h>
#include <qwt_symbol.h>

extern "C" Plugin::Object *createRTXIPlugin(void)
{
    return new MEA();
}

static DefaultGUIModel::variable_t vars[] = {
    { "Vm", "Membrane Voltage (in mV)", DefaultGUIModel::INPUT, },
	{ "Stimulation input", "Input waveform for stimulation", DefaultGUIModel::INPUT, },
	{ "Stimulation output", "Output waveform for stimulation", DefaultGUIModel::OUTPUT, },
    { "Max spike width (ms)", "Maximum spike duration",
        DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE, },
    { "Min spike width (ms)", "Minimum interval (refractory period) that must pass before another spike is detected",
        DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE, },
    { "Max spike amplitude (uV)", "Maximum spike amplitude in microvolts",
        DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE, },
    { "Min spike slope (uV/s)", "Minimum slope of a spike in microvolts per second",
        DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE, },
	{ "Refresh rate (s)", "Raster plot refresh rate", DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE, },
	{ "Note", "Time-stamped note to include in the output file", DefaultGUIModel::PARAMETER, },
	{ "Time (s)", "Time (s)", DefaultGUIModel::STATE, },
};

static size_t num_vars = sizeof(vars) / sizeof(DefaultGUIModel::variable_t);

// Default constructor
MEA::MEA(void) : DefaultGUIModel("MEA", ::vars, ::num_vars) {
	setWhatsThis(
		"<p><b>MEA:</b></p><p>This plug-in displays a raster plot of microelectrode array activity."
		" Click and drag on the plot to resize the axes.</p>");
	initParameters();
	DefaultGUIModel::createGUI(vars, num_vars); // this is required to create the GUI
	customizeGUI();
	update(INIT);
	refresh(); // this is required to update the GUI with parameter and state values
	QTimer::singleShot(0, this, SLOT(resizeMe()));
}

void MEA::customizeGUI(void)
{
    // TO-DO: allow plot to scale with module window
    QGridLayout *customLayout = DefaultGUIModel::getLayout();

    rplot = new BasicPlot(this);
    rplot->setAxisScaleDraw(QwtPlot::xBottom, new TimeScaleDraw(QTime(0,0,0,0)));
    rCurve = new QwtPlotCurve("Curve 1");
    rCurve->setStyle(QwtPlotCurve::NoCurve);
    rCurve->setSymbol(new QwtSymbol(QwtSymbol::VLine, Qt::NoBrush, QPen(Qt::white), QSize(4,4)));
    rCurve->attach(rplot);
    rCurve->setPen(QColor(Qt::white));

    QVBoxLayout *rightLayout = new QVBoxLayout;
    QGroupBox *plotBox = new QGroupBox("MEA Raster Plot");
    QHBoxLayout *plotBoxLayout = new QHBoxLayout;
    plotBox->setLayout(plotBoxLayout);
    QPushButton *savePlotButton = new QPushButton("Save Screenshot");
    QPushButton *clearButton = new QPushButton("Clear Plot");
    plotBoxLayout->addWidget(savePlotButton);
    plotBoxLayout->addWidget(clearButton);
    rightLayout->addWidget(rplot);

    QObject::connect(clearButton, SIGNAL(clicked()), this, SLOT(clearData()));
    QObject::connect(savePlotButton, SIGNAL(clicked()), this, SLOT(screenshot()));
    QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),this,SLOT(pause(bool)));
    QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),clearButton,SLOT(setEnabled(bool)));
    QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),DefaultGUIModel::modifyButton,SLOT(setEnabled(bool)));
    QObject::connect(this, SIGNAL(setPlotRange(double, double, double, double)), rplot, SLOT(setAxes(double, double, double, double)));

    DefaultGUIModel::pauseButton->setToolTip("Start/Stop protocol");
    DefaultGUIModel::modifyButton->setToolTip("Commit changes to parameter values");
    DefaultGUIModel::unloadButton->setToolTip("Close module");

    timer0->start(refreshRate * 1000); // max refresh rate = 4 Hz
    QObject::connect(timer0, SIGNAL(timeout(void)), this, SLOT(refreshMEA(void)));
    
    timer1->start(500); // spikeDetectWindow * 1000
    QObject::connect(timer1, SIGNAL(timeout(void)), this, SLOT(detectSpikes(void)));

    emit setPlotRange(0, systime, plotymin, plotymax);
    customLayout->addWidget(plotBox, 0, 0, 1, 2);
    customLayout->addLayout(rightLayout, 1, 1);
    setLayout(customLayout);
}

MEA::~MEA(void) {}

void MEA::execute(void) {
    systime = count * RT::System::getInstance()->getPeriod() * 1e-9; // current time

    // TO-DO: buffer systimes for the current vm buffer to get save accurate spike times
    
    // buffer voltage traces
	for (int i = 0; i < numChannels; i++) {
		vm[i].push(input(0));
	}
	numVoltageReads++;
	
	// TO-DO: need to make this channel specific (add a stim channel input)
	// stimulation output
	output(0) = input(1);
    
    count++; // increment count to measure time
    return;
}

void MEA::update(DefaultGUIModel::update_flags_t flag) {
    switch (flag) {
        case INIT:
            setState("Time (s)", systime);
            setParameter("Max spike width (ms)", QString::number(maxSpikeWidth * 1e3 / samplingFrequency));
            setParameter("Min spike width (ms)", QString::number(minSpikeWidth * 1e3 / samplingFrequency));
            setParameter("Max spike amplitude (uV)", QString::number(maxSpikeAmp * 1e6));
            setParameter("Min spike slope (uV/s)", QString::number(minSpikeSlope * 1e6));
            setParameter("Refresh rate (s)", QString::number(refreshRate));
            setParameter("Note", note);
            break;
        case MODIFY:
            maxSpikeWidth = floor(getParameter("Max spike width (ms)").toDouble() * samplingFrequency / 1e3);
            minSpikeWidth = floor(getParameter("Min spike width (ms)").toDouble() * samplingFrequency / 1e3);
            maxSpikeAmp = getParameter("Max spike amplitude (uV)").toDouble() / 1e6;
            minSpikeSlope = getParameter("Min spike slope (uV/s)").toDouble() / 1e6;
            refreshRate = getParameter("Refresh rate (s)").toDouble(); // To-do: constrain to > 4 Hz?
            bookkeep();
            break;
        case PAUSE:
            output(0) = 0; // stop command in case pause occurs in the middle of command
            break;
        case UNPAUSE:
            bookkeep();
            break;
        case PERIOD:
            dt = RT::System::getInstance()->getPeriod() * 1e-9;
            bookkeep();
            break;
        default:
            break;
    }
}

// custom functions
void MEA::initParameters() {
    systime = 0;
    count = 0;
    dt = RT::System::getInstance()->getPeriod() * 1e-9;
    refreshRate = 10; // max refresh rate = 4 Hz
    spikeDetectWindow = 500e-3;
    note = "";
    
    // spike validation variables
    threshPolarity = 0; // 0 = bipolar; 1 = negative only; 2 = positive only
	numPre = 15; // TO-DO: set this based on samplingFrequency (and possibly based on user input in msec)
	numPost = 15; // TO-DO: set this based on samplingFrequency
	maxSpikeWidth = floor(10e-3 * samplingFrequency);
	minSpikeWidth = floor(0.1e-3 * samplingFrequency);
	minSpikeSlope = 5e-6;
	maxSpikeAmp = 1000e-6;
    downsample = 1;
	deadTime = (int)(1e-3 * samplingFrequency);
    
    // spike detector variables
    spkcount = 0;
    numVoltageReads = 0;
    threshold.resize(numChannels);
    numUpdates.resize(numChannels);
    initialSamplesToSkip.resize(numChannels);
    regularDetect.resize(numChannels);
	spikeDetectionBuffer.reserve(vmBufferSize); // TO-DO: set size based on spike detector rate
	detectionCarryOverBuffer.resize(numChannels);
    RMSList.resize(numChannels);
    for(int j = 0; j < numChannels; j++) {
        RMSList[j].resize(numUpdatesForTrain);
    }
    inASpike.resize(numChannels);
    waitToComeDown.resize(numChannels);
    enterSpikeIndex.resize(numChannels);
    exitSpikeIndex.resize(numChannels);
    waveform.resize(numPost + numPre + 1);
    
    bookkeep();
}

void MEA::bookkeep() {
    timer0->start(refreshRate * 1000); // restart timer with new refreshRate
}

void MEA::refreshMEA() {
    for (int m = 0; m < spkcount; m++) {
        if(meaBuffer.pop(currentSpike)) {
            time.push_back(currentSpike.spktime);
            channels.push_back(currentSpike.channelNum);
        }
    }
    
    // delete old spikes
    if(!time.empty()) {
        while (time.front() < systime - displayTime) {
            time.pop_front();
            channels.pop_front();
        }
    }
    
    rCurve->setSamples(time, channels);
    
    if (systime <= displayTime) {
        emit setPlotRange(0, systime, plotymin, plotymax);
    } else {
        emit setPlotRange(systime-displayTime, systime, plotymin, plotymax);
    }
    rplot->replot();

    spkcount = 0;
}

void MEA::screenshot() {
    QwtPlotRenderer renderer;
    renderer.exportTo(rplot,"screenshot.pdf");
}

void MEA::clearData() {
	time.clear();
	channels.clear();
	
	rCurve->setSamples(time, channels);
	rplot->replot();
}

// spike detection/validation
void MEA::detectSpikes() {
    int channel;
    int indiciesToSearchForCross, indiciesToSearchForReturn;
	int idx, i;
    bool skipSpikeDetection;
    double spikeDetectionSum;
    
    for(channel = 0; channel < numChannels; channel++) {
        // define position in current data buffer
		i = numPre + initialSamplesToSkip[channel];
		initialSamplesToSkip[channel] = 0;
		
		// create the current data buffer
		if (!regularDetect[channel])
		{
			// first fill, cannot get the first samples because the number of "pre" samples will be too low
			regularDetect[channel] = true; // no longer the first detection
			spikeDetectionBuffer.clear();
			for (int j = 0; j < numVoltageReads; j++) {
				vm[channel].pop(vm_temp);
				spikeDetectionBuffer.prepend(vm_temp);
			}
		}
		else
		{
			spikeDetectionBuffer.clear();
			// data from this buffer
			for (int j = 0; j < numVoltageReads; j++) {
				vm[channel].pop(vm_temp);
				spikeDetectionBuffer.prepend(vm_temp);
			}
			// data from last buffer that we could not detect on because of edge effects
			for (int j = 0; j < carryOverLength; j++) {
				spikeDetectionBuffer.prepend(detectionCarryOverBuffer[channel][j]);
			}
		}
        
        // don't need to run spike detection if buffer is empty (protocol hasn't started) or all 0 (not acquiring data)
        skipSpikeDetection = false;
        spikeDetectionSum = 0;
        if (!spikeDetectionBuffer.empty()) {
            for (int j = 0; j < spikeDetectionBuffer.count(); j++) {
                spikeDetectionSum = spikeDetectionSum + spikeDetectionBuffer[j];
            }
            if (spikeDetectionSum == 0) {
                skipSpikeDetection = true;
            }
        } else {
            skipSpikeDetection = true;
        }
		
        if (!skipSpikeDetection) {
            indiciesToSearchForCross = spikeDetectionBuffer.count() - maxSpikeWidth - numPost;
            indiciesToSearchForReturn = spikeDetectionBuffer.count() - numPost;
        
            updateThreshold(channel); // update threshold for current channel
            
            // for fixed and adaptive, the current threshold is not a function of i
            currentThreshold = threshold[channel];
            for (; i < indiciesToSearchForReturn; i++)
            {
                //peak detection- just requires one sample
                if (!inASpike[channel] && i < indiciesToSearchForCross)
                {
                    if (withinThreshold(spikeDetectionBuffer[i],currentThreshold,threshPolarity))
                    {
                        waitToComeDown[channel] = false;
                        continue; // not above threshold, next point please
                    }
                    else if (!waitToComeDown[channel])
                    {
                        // entering a spike
                        inASpike[channel] = true;
                        enterSpikeIndex[channel] = i; // TO-DO: maybe use this index (either i or enterSpikeIndex) to set spike times?
                        posCross = findSpikePolarityBySlopeOfCrossing(channel);
                    }
                }
                // exiting a spike, maxspikewidth (to find peak), -numPre and +numPost (to find waveform)
                else if (inASpike[channel] &&
                        ((posCross && spikeDetectionBuffer[i] < currentThreshold) ||
                        (!posCross && spikeDetectionBuffer[i] > -currentThreshold)))
                {
                    inASpike[channel] = false;
                    exitSpikeIndex[channel] = i;
                    // calculate spike width
                    spikeWidth = exitSpikeIndex[channel] - enterSpikeIndex[channel];
                    // find the index and value of the spike maximum
                    spikeMaxIndex = findMaxDeflection(enterSpikeIndex[channel], spikeWidth);
                    spikeMax = spikeDetectionBuffer[spikeMaxIndex];
                    // define spike waveform
                    createWaveform(spikeMaxIndex);
                    // check if the spike is any good
                    goodSpike = checkSpike();
                    if (!goodSpike) {
                        continue; // if the spike is no good
                    }
                    // record the waveform
                    spike.spktime = systime; // TO-DO: this is probably no longer accurate by the time this code is reached
                    spike.channelNum = channel;
                    spike.currentThresh = currentThreshold;
                    spike.wave = waveform;
                    meaBuffer.push(spike);
                    spkcount++;
            
                    // Carry-over dead time if a spike was detected at the end of the buffer
                    initialSamplesToSkip[channel] = deadTime + numPre + (exitSpikeIndex[channel] - indiciesToSearchForCross);
                    if (initialSamplesToSkip[channel] < 0)
                        initialSamplesToSkip[channel] = 0;
            
                    //else
                    i = exitSpikeIndex[channel] + deadTime;
                }
                else if (inASpike[channel] && i == indiciesToSearchForReturn - 1)
                {
                    // spike is taking too long to come back through the threshold
                    waitToComeDown[channel] = true;
                    inASpike[channel] = false;
                    break;
                }
                else if (!inASpike[channel] && i >= indiciesToSearchForCross)
                {
                    break;
                }
            }
            // create carry-over buffer from last samples of this buffer
            idx = 0;
            for (i = spikeDetectionBuffer.count() - carryOverLength; i < spikeDetectionBuffer.count(); i++)
            {
                detectionCarryOverBuffer[channel][idx] = spikeDetectionBuffer[i];
                idx++;
            }
        }
    }
    numVoltageReads = 0;
}

void MEA::updateThreshold(int channel)
{
    //std::cout << "updateThreshold -- input(0): " << input(0) << std::endl;
    // start updating threshold once data acquisition starts
    if (!spikeDetectionBuffer.empty())
    {
        if (numUpdates[channel] > numUpdatesForTrain) { /* do nothing */ }
        else if (numUpdates[channel] == numUpdatesForTrain)
        {
            // average threshold estimates gathered during training period
            for(int j = 0; j < RMSList[channel].size(); j++) {
                threshold[channel] += RMSList[channel][j];
            }
            threshold[channel] /= RMSList[channel].size();
            numUpdates[channel]++; // prevent further updates
        }
        else
        {
            calcThreshForOneBlock(channel);
            numUpdates[channel]++;
        }
    }
}

void MEA::calcThreshForOneBlock(int channel)
{
    double dd;
    double tempData = 0;
    double thresholdTemp;
    for (int j = 0; j < spikeDetectionBuffer.size() / downsample; j++)
    {
        dd = spikeDetectionBuffer[j * downsample] * spikeDetectionBuffer[j * downsample];
        if (dd > 0) // don't include blanked samples
        {
            tempData += dd;
        }
    }
    tempData /= (spikeDetectionBuffer.size() / downsample);
    //std::cout << "calcThreshForOneBlock -- tempData: " << tempData << std::endl;
    thresholdTemp = sqrt(tempData); // TO-DO: * _thresholdMultiplier; What is this for?
    //std::cout << "calcThreshForOneBlock -- thresholdTemp: " << thresholdTemp << std::endl;
    RMSList[channel][numUpdates[channel]] = thresholdTemp;
    //std::cout << "calcThreshForOneBlock -- threshold on channel " << channel << ": " << threshold[channel] << std::endl;
    threshold[channel] = ((threshold[channel] * (numUpdates[channel])) / (numUpdates[channel] + 1)) + (thresholdTemp / (numUpdates[channel] + 1));
    //std::cout << "calcThreshForOneBlock -- numUpdates on channel " << channel << ": " << numUpdates[channel] << std::endl;
    //std::cout << "calcThreshForOneBlock -- threshold on channel " << channel << ": " << threshold[channel] << std::endl;
}


bool MEA::withinThreshold(double channelVoltage, double thisThreshold, int threshPolarity)
{
	switch(threshPolarity)
	{
		case 0:
			return channelVoltage < thisThreshold && channelVoltage > -thisThreshold;
		case 1:
			return channelVoltage > -thisThreshold;
		case 2:
			return channelVoltage < thisThreshold;
		default:
			return channelVoltage < thisThreshold && channelVoltage > -thisThreshold;
	}
}

bool MEA::findSpikePolarityBySlopeOfCrossing(int channel)
{
	// Is the crossing through the bottom or top threshold?
	return spikeDetectionBuffer[enterSpikeIndex[channel]] > 0;
}

int MEA::findMaxDeflection(int startInd, int widthToSearch)
{
	int maxIndex = startInd;
	// Find absolute maximum
	for (int i = startInd+1; i < startInd + widthToSearch; i++) {
        if (abs(spikeDetectionBuffer[i]) > abs(spikeDetectionBuffer[maxIndex])) {
			maxIndex = i;
		}
	}
	return maxIndex;
}

void MEA::createWaveform(int maxIdx)
{
	for (int j = maxIdx - numPre; j < maxIdx + numPost + 1; j++) {
		waveform[j - maxIdx + numPre] = spikeDetectionBuffer[j];
    }
}

// Check spike based on spike detection settings
bool MEA::checkSpike()
{
	// Check spike width
	bool spikeWidthGood = maxSpikeWidth >= spikeWidth && minSpikeWidth <= spikeWidth;
	if (!spikeWidthGood) {
        //std::cout << "Here0" << std::endl;
		return spikeWidthGood;
    }
	
	// Check spike amplitude
    // TO-DO: absWave is all 0 -- abs function is not working as expected
	QVector<double> absWave;
	absWave.resize(waveform.size());
	for (int i = 0; i < waveform.size(); ++i) {
		absWave[i] = qAbs(waveform[i]);
    }

	bool spikeMaxGood = spikeMax < maxSpikeAmp; // this has already been calculated
	if (!spikeMaxGood) {
        //std::cout << "Here1" << std::endl;
		return spikeMaxGood;
    }

	// Check to make sure this is not the tail end of another spike
	bool notTailend = absWave[0] < absWave[numPre];
    //std::cout << "absWave[0]: " << absWave[0] << std::endl << "absWave[numPre]: " << absWave[numPre] << std::endl;
	if (!notTailend) {
        //std::cout << "Here2" << std::endl;
		return notTailend;
    }

	// Check spike slope
    // TO-DO: currently debugging here -- slope is always 0
	bool spikeSlopeGood = getSpikeSlope(absWave) > minSpikeSlope;
	if (!spikeSlopeGood) {
        //std::cout << "Spike slope: " << getSpikeSlope(absWave) << std::endl;
        //std::cout << "Here3" << std::endl;
		return spikeSlopeGood;
    }

	//Ensure that part of the spike is not blanked
	double numBlanked = 0;
	for (int i = 0; i < absWave.size(); i++)
	{
		if (absWave[i] < VOLTAGE_EPSILON)
		{
			numBlanked++;
		}
		else
		{
			numBlanked = 0;
		}

		if (numBlanked > 5) {
			return false;
        }
	}

	// spike is validated
	return true;
}

double MEA::getSpikeSlope(QVector<double> absWave)
{
	double spikeSlopeEstimate = 0;
	int diffWidth;

	if (spikeWidth + 2 <= numPre)
		diffWidth = spikeWidth + 2;
	else
		diffWidth = numPre;

	for (int i = numPre + 1 - diffWidth; i < numPre + diffWidth; i++)
	{
		spikeSlopeEstimate += qAbs(absWave[i + 1] - absWave[i]);
	}

	return spikeSlopeEstimate / (double)(2 * diffWidth);
}

