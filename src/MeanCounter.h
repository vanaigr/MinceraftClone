#pragma once

template<size_t size>
class MeanCounter {
private:
	double accumulator[size];
	size_t currentIndex = 0;
	size_t curSize = 0;
public:

	void add(double val) {
		accumulator[currentIndex] = val;
		currentIndex = (currentIndex+1)%size;
		if(curSize < size) curSize++;
	}
	
	size_t index() const {
		return currentIndex;
	}

	double mean() const {
		double value = 0;
		for (size_t i = 0; i < curSize; i++) {
			value += accumulator[i];
		}
		return value / curSize;
	}

	double min() const {
		if (curSize == 0) {
			return 0.0;
		}
		else {
			double min = accumulator[0];
			for (size_t i = 1; i < curSize; i++) {
				if(min > accumulator[i]) min = accumulator[i];
			}
			return min;
		}
	}

	double max() const {
		if (curSize == 0) {
			return 0.0;
		}
		else {
			double max = accumulator[0];
			for (size_t i = 1; i < curSize; i++) {
				if (max < accumulator[i]) max = accumulator[i];
			}
			return max;
		}
	}

	void printall(std::ostream &stream) const {
		for (auto el : accumulator) {
			stream << el << std::endl;
		}
	}
};