#include "Schema.h"
#include "../../Helper/assert.h"

bool Collector::computeVisibleLines() {
    bool modified = false;
    for (const auto &line: lines) {
        if (withInput.count(line) == 0 && outputs.count(line.start) > 0) {
            withInput.insert(line); // the cable is connected to a real output
            outputs.insert(line.end); // end become a real output too
            modified = true;
        }
        if (withOutput.count(line) == 0 && inputs.count(line.end) > 0) {
            withOutput.insert(line); // the cable is connected to a real input
            inputs.insert(line.start); // start become a real input too
            modified = true;
        }
    }
    return modified;
}

void Collector::draw(Device &device) {
    while (computeVisibleLines()) {}
    for (const auto &line: lines) if (withInput.count(line) && withOutput.count(line)) line.draw(device);
}

// A simple rectangular box with a text and inputs and outputs.
// The constructor is private in order to make sure `makeBlockSchema` is used instead
struct BlockSchema : Schema {
    friend Schema *makeBlockSchema(unsigned int inputs, unsigned int outputs, const string &text, const string &color, const string &link);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

protected:
    BlockSchema(unsigned int inputs, unsigned int outputs, double width, double height, string text, string color, string link);

    const string text;
    const string color;
    const string link;

    vector<Point> inputPoints;
    vector<Point> outputPoints;
};

static inline double quantize(int n) {
    static const int q = 3;
    return q * ((n + q - 1) / q); // NOLINT(bugprone-integer-division)
}

// Build a simple colored `BlockSchema` with a certain number of inputs and outputs, a text to be displayed, and an optional link.
// Computes the size of the box according to the length of the text and the maximum number of ports.
Schema *makeBlockSchema(unsigned int inputs, unsigned int outputs, const string &text, const string &color, const string &link) {
    const double minimal = 3 * dWire;
    const double w = 2 * dHorz + max(minimal, dLetter * quantize((int) text.size()));
    const double h = 2 * dVert + max(minimal, max(inputs, outputs) * dWire);
    return new BlockSchema(inputs, outputs, w, h, text, color, link);
}

BlockSchema::BlockSchema(unsigned int inputs, unsigned int outputs, double width, double height, string text, string color, string link)
    : Schema(inputs, outputs, width, height), text(std::move(text)), color(std::move(color)), link(std::move(link)) {
    for (unsigned int i = 0; i < inputs; i++) inputPoints.emplace_back(0, 0);
    for (unsigned int i = 0; i < outputs; i++) outputPoints.emplace_back(0, 0);
}

void BlockSchema::placeImpl() {
    const bool isLR = orientation == kLeftRight;
    for (unsigned int i = 0; i < inputs; i++) inputPoints[i] = {isLR ? x : x + width, y + height / 2.0 - dWire * ((inputs - 1) / 2.0 + i * (isLR ? -1.0 : 1.0))};
    for (unsigned int i = 0; i < outputs; i++) outputPoints[i] = {isLR ? x + width : x, y + height / 2.0 - dWire * ((outputs - 1) / 2.0 + i * (isLR ? -1.0 : 1.0))};
}

Point BlockSchema::inputPoint(unsigned int i) const { return inputPoints[i]; }
Point BlockSchema::outputPoint(unsigned int i) const { return outputPoints[i]; }

void BlockSchema::draw(Device &device) {
    device.rect(x + dHorz, y + dVert, width - 2 * dHorz, height - 2 * dVert, color, link);
    device.text(x + width / 2, y + height / 2, text.c_str(), link);

    // Draw a small point that indicates the first input (like an integrated circuits).
    const bool isLR = orientation == kLeftRight;
    device.dot(x + (isLR ? dHorz : (width - dHorz)), y + (isLR ? dVert : (height - dVert)), orientation);

    // Input arrows
    const double dx = isLR ? dHorz : -dHorz;
    for (const auto &p: inputPoints) device.arrow(p.x + dx, p.y, 0, orientation);
}

void BlockSchema::collectLines(Collector &c) {
    const double dx = orientation == kLeftRight ? dHorz : -dHorz;
    // Input wires
    for (const auto &p: inputPoints) {
        c.addLine({p, {p.x + dx, p.y}});
        c.addInput({p.x + dx, p.y});
    }
    // Output wires
    for (const auto &p: outputPoints) {
        c.addLine({{p.x - dx, p.y}, p});
        c.addOutput({p.x - dx, p.y});
    }
}

// Simple cables (identity box) in parallel.
// The width of a cable is null.
// Therefor, input and output connection points are the same.
// The constructor is private to enforce the use of `makeCableSchema`.
struct CableSchema : Schema {
    friend Schema *makeCableSchema(unsigned int n);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    CableSchema(unsigned int n);

    vector<Point> points;
};

Schema *makeCableSchema(unsigned int n) { return new CableSchema(n); }

CableSchema::CableSchema(unsigned int n) : Schema(n, n, 0, n * dWire) {
    for (unsigned int i = 0; i < n; i++) points.emplace_back(0, 0);
}

// Place the communication points vertically spaced by `dWire`.
void CableSchema::placeImpl() {
    for (unsigned int i = 0; i < inputs; i++) {
        const double dx = dWire * (i + 0.5);
        points[i] = {x, y + (orientation == kLeftRight ? dx : (height - dx))};
    }
}

// Nothing to draw.
// Actual drawing will take place when the wires are enlarged.
void CableSchema::draw(Device &) {}

// Nothing to collect.
// Actual collect will take place when the wires are enlarged.
void CableSchema::collectLines(Collector &) {}

// Input and output points are the same if the width is 0.
Point CableSchema::inputPoint(unsigned int i) const { return points[i]; }

// Input and output points are the same if the width is 0.
Point CableSchema::outputPoint(unsigned int i) const { return points[i]; }

// An inverter is a special symbol corresponding to '*(-1)' to create more compact diagrams.
struct InverterSchema : BlockSchema {
    friend Schema *makeInverterSchema(const string &color);
    void draw(Device &) override;

private:
    InverterSchema(const string &color);
};

Schema *makeInverterSchema(const string &color) { return new InverterSchema(color); }

InverterSchema::InverterSchema(const string &color) : BlockSchema(1, 1, 2.5 * dWire, dWire, "-1", color, "") {}

void InverterSchema::draw(Device &device) {
    device.triangle(x + dHorz, y + 0.5, width - 2 * dHorz, height - 1, color, orientation, link);
}

// Terminate a cable (cut box).
struct CutSchema : Schema {
public:
    friend Schema *makeCutSchema();
    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    CutSchema();
    Point point;
};

Schema *makeCutSchema() { return new CutSchema(); }

// A Cut is represented by a small black dot.
// It has 1 input and no outputs.
// It has a 0 width and a 1 wire height.
// The constructor is private in order to enforce the usage of `makeCutSchema`.
CutSchema::CutSchema() : Schema(1, 0, 0, dWire / 100.0), point(0, 0) {}

// The input point is placed in the middle.
void CutSchema::placeImpl() {
    point = {x, y + height * 0.5};
}

// A cut is represented by a small black dot.
void CutSchema::draw(Device &) {
    // dev.rond(point.x, point.y, dWire/8.0);
}

void CutSchema::collectLines(Collector &) {}

// By definition, a Cut has only one input point.
Point CutSchema::inputPoint(unsigned int) const { return point; }

// By definition, a Cut has no output point.
Point CutSchema::outputPoint(unsigned int) const {
    assert(false);
    return {-1, -1};
}

struct EnlargedSchema : Schema {
    EnlargedSchema(Schema *s, double width);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    Schema *schema;
    vector<Point> inputPoints;
    vector<Point> outputPoints;
};

// Returns an enlarged schema, but only if really needed.
// That is, if the required width is greater that the schema width.
Schema *makeEnlargedSchema(Schema *s, double width) {
    return width > s->width ? new EnlargedSchema(s, width) : s;
}

// Put additional space left and right of a schema so that the result has a certain width.
// The wires are prolonged accordingly.
EnlargedSchema::EnlargedSchema(Schema *s, double width)
    : Schema(s->inputs, s->outputs, width, s->height), schema(s) {
    for (unsigned int i = 0; i < inputs; i++) inputPoints.emplace_back(0, 0);
    for (unsigned int i = 0; i < outputs; i++) outputPoints.emplace_back(0, 0);
}

void EnlargedSchema::placeImpl() {
    double dx = (width - schema->width) / 2;
    schema->place(x + dx, y, orientation);

    if (orientation == kRightLeft) dx = -dx;

    for (unsigned int i = 0; i < inputs; i++) {
        const auto p = schema->inputPoint(i);
        inputPoints[i] = {p.x - dx, p.y};
    }
    for (unsigned int i = 0; i < outputs; i++) {
        const auto p = schema->outputPoint(i);
        outputPoints[i] = {p.x + dx, p.y};
    }
}

Point EnlargedSchema::inputPoint(unsigned int i) const { return inputPoints[i]; }
Point EnlargedSchema::outputPoint(unsigned int i) const { return outputPoints[i]; }

void EnlargedSchema::draw(Device &device) { schema->draw(device); }

void EnlargedSchema::collectLines(Collector &c) {
    schema->collectLines(c);
    for (unsigned int i = 0; i < inputs; i++) c.addLine({inputPoint(i), schema->inputPoint(i)});
    for (unsigned int i = 0; i < outputs; i++) c.addLine({schema->outputPoint(i), outputPoint(i)});
}

struct ParallelSchema : Schema {
    ParallelSchema(Schema *s1, Schema *s2);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    Schema *schema1;
    Schema *schema2;
    unsigned int inputFrontier;
    unsigned int outputFrontier;
};

// Make sure s1 and s2 have same width.
Schema *makeParallelSchema(Schema *s1, Schema *s2) {
    return new ParallelSchema(makeEnlargedSchema(s1, s2->width), makeEnlargedSchema(s2, s1->width));
}

ParallelSchema::ParallelSchema(Schema *s1, Schema *s2)
    : Schema(s1->inputs + s2->inputs, s1->outputs + s2->outputs, s1->width, s1->height + s2->height),
      schema1(s1), schema2(s2), inputFrontier(s1->inputs), outputFrontier(s1->outputs) {
    assert(s1->width == s2->width);
}

void ParallelSchema::placeImpl() {
    if (orientation == kLeftRight) {
        schema1->place(x, y, orientation);
        schema2->place(x, y + schema1->height, orientation);
    } else {
        schema2->place(x, y, orientation);
        schema1->place(x, y + schema2->height, orientation);
    }
}

Point ParallelSchema::inputPoint(unsigned int i) const {
    return i < inputFrontier ? schema1->inputPoint(i) : schema2->inputPoint(i - inputFrontier);
}

Point ParallelSchema::outputPoint(unsigned int i) const {
    return i < outputFrontier ? schema1->outputPoint(i) : schema2->outputPoint(i - outputFrontier);
}

void ParallelSchema::draw(Device &device) {
    schema1->draw(device);
    schema2->draw(device);
}

void ParallelSchema::collectLines(Collector &c) {
    schema1->collectLines(c);
    schema2->collectLines(c);
}

struct SequentialSchema : Schema {
    friend Schema *makeSequentialSchema(Schema *s1, Schema *s2);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    SequentialSchema(Schema *s1, Schema *s2, double hgap);
    void collectInternalWires(Collector &);

    Schema *schema1;
    Schema *schema2;
    double horzGap;
};

enum { kHorDir, kUpDir, kDownDir };  // directions of connections

// Compute the direction of a connection. Note that
// Y axis goes from top to bottom
static int direction(const Point &a, const Point &b) {
    if (a.y > b.y) return kUpDir;    // upward connections
    if (a.y < b.y) return kDownDir;  // downward connection
    return kHorDir;                  // horizontal connections
}

// Compute the horizontal gap needed to draw the internal wires.
// It depends on the largest group of connections that go in the same direction.
static double computeHorzGap(Schema *a, Schema *b) {
    assert(a->outputs == b->inputs);

    if (a->outputs == 0) return 0;

    // store the size of the largest group for each direction
    int MaxGroupSize[3];
    for (int &i: MaxGroupSize) i = 0;

    // place a and b to have valid connection points
    a->place(0, max(0.0, 0.5 * (b->height - a->height)), kLeftRight);
    b->place(0, max(0.0, 0.5 * (a->height - b->height)), kLeftRight);

    // init current group direction and size
    int gdir = direction(a->outputPoint(0), b->inputPoint(0));
    int gsize = 1;

    // analyze direction of remaining points
    for (unsigned int i = 1; i < a->outputs; i++) {
        int d = direction(a->outputPoint(i), b->inputPoint(i));
        if (d == gdir) {
            gsize++;
        } else {
            if (gsize > MaxGroupSize[gdir]) MaxGroupSize[gdir] = gsize;
            gsize = 1;
            gdir = d;
        }
    }

    // update for last group
    if (gsize > MaxGroupSize[gdir]) MaxGroupSize[gdir] = gsize;

    // the gap required for the connections
    return dWire * max(MaxGroupSize[kUpDir], MaxGroupSize[kDownDir]);
}

// May add cables to ensure the internal connections are between the same number of outputs and inputs.
// Compute a horizontal gap based on the number of upward and downward connections.
Schema *makeSequentialSchema(Schema *s1, Schema *s2) {
    const unsigned int o = s1->outputs;
    const unsigned int i = s2->inputs;
    auto *a = (o < i) ? makeParallelSchema(s1, makeCableSchema(i - o)) : s1;
    auto *b = (o > i) ? makeParallelSchema(s2, makeCableSchema(o - i)) : s2;

    return new SequentialSchema(a, b, computeHorzGap(a, b));
}

// Constructor for a sequential schema (s1:s2).
// The components s1 and s2 are supposed to be "compatible" (s1 : n->m and s2 : m->q).
SequentialSchema::SequentialSchema(Schema *s1, Schema *s2, double hgap)
    : Schema(s1->inputs, s2->outputs, s1->width + hgap + s2->width, max(s1->height, s2->height)), schema1(s1), schema2(s2), horzGap(hgap) {
    assert(s1->outputs == s2->inputs);
}

// Place the two components horizontally with enough space for the connections.
void SequentialSchema::placeImpl() {
    const double y1 = max(0.0, 0.5 * (schema2->height - schema1->height));
    const double y2 = max(0.0, 0.5 * (schema1->height - schema2->height));
    if (orientation == kLeftRight) {
        schema1->place(x, y + y1, orientation);
        schema2->place(x + schema1->width + horzGap, y + y2, orientation);
    } else {
        schema2->place(x, y + y2, orientation);
        schema1->place(x + schema2->width + horzGap, y + y1, orientation);
    }
}

// The input points are the input points of the first component.
Point SequentialSchema::inputPoint(unsigned int i) const { return schema1->inputPoint(i); }

// The output points are the output points of the second component.
Point SequentialSchema::outputPoint(unsigned int i) const { return schema2->outputPoint(i); }

void SequentialSchema::draw(Device &device) {
    assert(schema1->outputs == schema2->inputs);
    schema1->draw(device);
    schema2->draw(device);
}

void SequentialSchema::collectLines(Collector &c) {
    assert(schema1->outputs == schema2->inputs);
    schema1->collectLines(c);
    schema2->collectLines(c);
    collectInternalWires(c);
}

// Draw the internal wires aligning the vertical segments in a symmetric way when possible.
void SequentialSchema::collectInternalWires(Collector &c) {
    const unsigned int N = schema1->outputs;
    assert(N == schema2->inputs);

    double dx = 0, mx = 0;
    int dir = -1;
    for (unsigned int i = 0; i < N; i++) {
        const auto src = schema1->outputPoint(i);
        const auto dst = schema2->inputPoint(i);
        const int d = direction(src, dst);
        if (d == dir) {
            mx += dx; // move in same direction
        } else {
            mx = orientation == kLeftRight ? (d == kDownDir ? horzGap : 0) : (d == kUpDir ? -horzGap : 0);
            dx = d == kUpDir ? dWire : d == kDownDir ? -dWire : 0;
            dir = d;
        }
        // todo add a toggle to always draw the straight cable - I tried this and it can look better imo (diagonal lines instead of manhatten)
        if (src.y == dst.y) {
            // Draw a straight, potentially diagonal cable.
            c.addLine({src, dst});
        } else {
            // Draw a zigzag cable by traversing half the distance between, taking a sharp turn, then turning back and finishing.
            c.addLine({src, {src.x + mx, src.y}});
            c.addLine({{src.x + mx, src.y}, {src.x + mx, dst.y}});
            c.addLine({{src.x + mx, dst.y}, dst});
        }
    }
}

// Place and connect two diagrams in merge composition.
struct MergeSchema : Schema {
    friend Schema *makeMergeSchema(Schema *s1, Schema *s2);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    MergeSchema(Schema *s1, Schema *s2, double hgap);

    Schema *schema1;
    Schema *schema2;
    double horzGap;
};

// Cables are enlarged to `dWire`.
Schema *makeMergeSchema(Schema *s1, Schema *s2) {
    auto *a = makeEnlargedSchema(s1, dWire);
    auto *b = makeEnlargedSchema(s2, dWire);

    // Horizontal gap to avoid sloppy connections.
    return new MergeSchema(a, b, (a->height + b->height) / 4);
}

// Constructor for a merge schema s1 :> s2 where the outputs of s1 are merged to the inputs of s2.
// The constructor is private in order to enforce the usage of `makeMergeSchema`.
MergeSchema::MergeSchema(Schema *s1, Schema *s2, double hgap)
    : Schema(s1->inputs, s2->outputs, s1->width + s2->width + hgap, max(s1->height, s2->height)), schema1(s1), schema2(s2), horzGap(hgap) {}

// Place the two subschema horizontaly, centered, with enough gap for the connections.
void MergeSchema::placeImpl() {
    const double dy1 = max(0.0, schema2->height - schema1->height) / 2.0;
    const double dy2 = max(0.0, schema1->height - schema2->height) / 2.0;
    if (orientation == kLeftRight) {
        schema1->place(x, y + dy1, orientation);
        schema2->place(x + schema1->width + horzGap, y + dy2, orientation);
    } else {
        schema2->place(x, y + dy2, orientation);
        schema1->place(x + schema2->width + horzGap, y + dy1, orientation);
    }
}

// The inputs of s1 :> s2 are the inputs of s1.
Point MergeSchema::inputPoint(unsigned int i) const { return schema1->inputPoint(i); }

// The outputs of s1 :> s2 are the outputs of s2.
Point MergeSchema::outputPoint(unsigned int i) const { return schema2->outputPoint(i); }

void MergeSchema::draw(Device &device) {
    schema1->draw(device);
    schema2->draw(device);
}

void MergeSchema::collectLines(Collector &c) {
    schema1->collectLines(c);
    schema2->collectLines(c);
    for (unsigned int i = 0; i < schema1->outputs; i++) c.addLine({schema1->outputPoint(i), schema2->inputPoint(i % schema2->inputs)});
}

// Place and connect two diagrams in split composition.
struct SplitSchema : Schema {
    friend Schema *makeSplitSchema(Schema *s1, Schema *s2);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    SplitSchema(Schema *s1, Schema *s2, double hgap);

    Schema *schema1;
    Schema *schema2;
    double horzGap;
};

// Cables are enlarged to `dWire`.
Schema *makeSplitSchema(Schema *s1, Schema *s2) {
    // Make sure `a` and `b` are at least `dWire` large.
    auto *a = makeEnlargedSchema(s1, dWire);
    auto *b = makeEnlargedSchema(s2, dWire);

    // Horizontal gap to avoid sloppy connections.
    return new SplitSchema(a, b, (a->height + b->height) / 4);
}

// Constructor for a split schema s1 <: s2, where the outputs of s1 are distributed to the inputs of s2.
// The constructor is private in order to enforce the usage of `makeSplitSchema`.
SplitSchema::SplitSchema(Schema *s1, Schema *s2, double hgap)
    : Schema(s1->inputs, s2->outputs, s1->width + s2->width + hgap, max(s1->height, s2->height)),
      schema1(s1), schema2(s2), horzGap(hgap) {}

// Place the two subschema horizontaly, centered, with enough gap for the connections
void SplitSchema::placeImpl() {
    const double dy1 = max(0.0, schema2->height - schema1->height) / 2.0;
    const double dy2 = max(0.0, schema1->height - schema2->height) / 2.0;
    if (orientation == kLeftRight) {
        schema1->place(x, y + dy1, orientation);
        schema2->place(x + schema1->width + horzGap, y + dy2, orientation);
    } else {
        schema2->place(x, y + dy2, orientation);
        schema1->place(x + schema2->width + horzGap, y + dy1, orientation);
    }
}

// The inputs of s1 <: s2 are the inputs of s1.
Point SplitSchema::inputPoint(unsigned int i) const { return schema1->inputPoint(i); }

// The outputs of s1 <: s2 are the outputs of s2.
Point SplitSchema::outputPoint(unsigned int i) const { return schema2->outputPoint(i); }

void SplitSchema::draw(Device &device) {
    schema1->draw(device);
    schema2->draw(device);
}

void SplitSchema::collectLines(Collector &c) {
    schema1->collectLines(c);
    schema2->collectLines(c);
    for (unsigned int i = 0; i < schema2->inputs; i++) c.addLine({schema1->outputPoint(i % schema1->outputs), schema2->inputPoint(i)});
}


// Place and connect two diagrams in recursive composition
struct RecSchema : Schema {
    friend Schema *makeRecSchema(Schema *s1, Schema *s2);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    RecSchema(Schema *s1, Schema *s2, double width);

    void collectFeedback(Collector &c, const Point &src, const Point &dst, double dx, const Point &out);
    void collectFeedfront(Collector &c, const Point &src, const Point &dst, double dx);

    Schema *schema1;
    Schema *schema2;
    vector<Point> inputPoints;
    vector<Point> outputPoints;
};

// Creates a new recursive schema (s1 ~ s2).
// The smallest component is enlarged to the width of the other.
// The left and right horizontal margins are computed according to the number of internal connections.
Schema *makeRecSchema(Schema *s1, Schema *s2) {
    auto *a = makeEnlargedSchema(s1, s2->width);
    auto *b = makeEnlargedSchema(s2, s1->width);
    const double w = a->width + 2 * (dWire * max(b->inputs, b->outputs));

    return new RecSchema(a, b, w);
}

// Constructor of a recursive schema (s1 ~ s2).
// The two components are supposed to have the same width.
RecSchema::RecSchema(Schema *s1, Schema *s2, double width)
    : Schema(s1->inputs - s2->outputs, s1->outputs, width, s1->height + s2->height), schema1(s1), schema2(s2) {
    // This version only accepts legal expressions of same width.
    assert(s1->inputs >= s2->outputs);
    assert(s1->outputs >= s2->inputs);
    assert(s1->width >= s2->width);

    for (unsigned int i = 0; i < inputs; i++) inputPoints.emplace_back(0, 0);
    for (unsigned int i = 0; i < outputs; i++) outputPoints.emplace_back(0, 0);
}

// The two subschema are placed centered vertically, s2 on top of s1.
// The input and output points are computed.
void RecSchema::placeImpl() {
    double dx1 = (width - schema1->width) / 2;
    const double dx2 = (width - schema2->width) / 2;
    if (orientation == kLeftRight) {
        schema2->place(x + dx2, y, kRightLeft);
        schema1->place(x + dx1, y + schema2->height, kLeftRight);
    } else {
        schema1->place(x + dx1, y, kRightLeft);
        schema2->place(x + dx2, y + schema1->height, kLeftRight);
    }

    // adjust delta space to orientation
    if (orientation == kRightLeft) dx1 = -dx1;

    for (unsigned int i = 0; i < inputs; i++) {
        const auto p = schema1->inputPoint(i + schema2->outputs);
        inputPoints[i] = {p.x - dx1, p.y};
    }
    for (unsigned int i = 0; i < outputs; i++) {
        const auto p = schema1->outputPoint(i);
        outputPoints[i] = {p.x + dx1, p.y};
    }
}

// The input/output points s1 ~ s2
Point RecSchema::inputPoint(unsigned int i) const { return inputPoints[i]; }
Point RecSchema::outputPoint(unsigned int i) const { return outputPoints[i]; }

// Draw the delay sign of a feedback connection
static void drawDelaySign(Device &dev, double x, double y, double size) {
    dev.line(x - size / 2, y, x - size / 2, y - size);
    dev.line(x - size / 2, y - size, x + size / 2, y - size);
    dev.line(x + size / 2, y - size, x + size / 2, y);
}

void RecSchema::draw(Device &device) {
    schema1->draw(device);
    schema2->draw(device);

    // Draw the implicit feedback delay to each schema2 input
    const double dw = (orientation == kLeftRight) ? dWire : -dWire;
    for (unsigned int i = 0; i < schema2->inputs; i++) {
        const auto &p = schema1->outputPoint(i);
        drawDelaySign(device, p.x + i * dw, p.y, dw / 2);
    }
}

void RecSchema::collectLines(Collector &c) {
    schema1->collectLines(c);
    schema2->collectLines(c);

    // Feedback connections to each schema2 input
    for (unsigned int i = 0; i < schema2->inputs; i++) collectFeedback(c, schema1->outputPoint(i), schema2->inputPoint(i), i * dWire, outputPoint(i));

    // Non-recursive output lines
    for (unsigned int i = schema2->inputs; i < outputs; i++) c.addLine({schema1->outputPoint(i), outputPoint(i)});

    // Input lines
    const unsigned int skip = schema2->outputs;
    for (unsigned int i = 0; i < inputs; i++) c.addLine({inputPoint(i), schema1->inputPoint(i + skip)});

    // Feedfront connections from each schema2 output
    for (unsigned int i = 0; i < schema2->outputs; i++) collectFeedfront(c, schema2->outputPoint(i), schema1->inputPoint(i), i * dWire);
}

// Draw a feedback connection between two points with a horizontal displacement `dx`.
void RecSchema::collectFeedback(Collector &c, const Point &src, const Point &dst, double dx, const Point &out) {
    const double ox = src.x + ((orientation == kLeftRight) ? dx : -dx);
    const double ct = (orientation == kLeftRight) ? dWire / 2.0 : -dWire / 2.0;
    const Point up(ox, src.y - ct);
    const Point br(ox + ct / 2.0, src.y);

    c.addOutput(up);
    c.addOutput(br);
    c.addInput(br);

    c.addLine({up, {ox, dst.y}});
    c.addLine({{ox, dst.y}, dst});
    c.addLine({src, br});
    c.addLine({br, out});
}

// Draw a feedfrom connection between two points with a horizontal displacement `dx`.
void RecSchema::collectFeedfront(Collector &c, const Point &src, const Point &dst, double dx) {
    const double ox = src.x + (orientation == kLeftRight ? -dx : dx);
    c.addLine({{src.x, src.y}, {ox, src.y}});
    c.addLine({{ox, src.y}, {ox, dst.y}});
    c.addLine({{ox, dst.y}, {dst.x, dst.y}});
}

// A TopSchema is a schema surrounded by a dashed rectangle with a label on the top left.
// The rectangle is placed at half the margin parameter.
// Arrows are added to all the outputs.
struct TopSchema : Schema {
    friend Schema *makeTopSchema(Schema *s1, double margin, const string &text, const string &link);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    TopSchema(Schema *s1, double margin, string text, string link);

    Schema *schema;
    double fMargin;
    string text;
    string link;
    vector<Point> inputPoints;
    vector<Point> outputPoints;
};

Schema *makeTopSchema(Schema *s, double margin, const string &text, const string &link) {
    return new TopSchema(makeDecorateSchema(s, margin / 2, text), margin / 2, "", link);
}

// A TopSchema is a schema surrounded by a dashed rectangle with a label on the top left.
// The rectangle is placed at half the margin parameter.
// Arrows are added to the outputs.
// The constructor is made private to enforce the usage of `makeTopSchema`.
TopSchema::TopSchema(Schema *s, double margin, string text, string link)
    : Schema(0, 0, s->width + 2 * margin, s->height + 2 * margin), schema(s), fMargin(margin), text(std::move(text)), link(std::move(link)) {}

void TopSchema::placeImpl() {
    schema->place(x + fMargin, y + fMargin, orientation);
}

// Top schema has no input or output
Point TopSchema::inputPoint(unsigned int) const { throw std::runtime_error("ERROR : TopSchema::inputPoint"); }
Point TopSchema::outputPoint(unsigned int) const { throw std::runtime_error("ERROR : TopSchema::outputPoint"); }

void TopSchema::draw(Device &device) {
    device.rect(x, y, width - 1, height - 1, "#ffffff", link);
    device.label(x + fMargin, y + fMargin / 2, text.c_str());

    schema->draw(device);

    for (unsigned int i = 0; i < schema->outputs; i++) {
        const auto p = schema->outputPoint(i);
        device.arrow(p.x, p.y, 0, orientation);
    }
}

void TopSchema::collectLines(Collector &c) {
    schema->collectLines(c);
    for (unsigned int i = 0; i < schema->inputs; i++) c.addOutput(schema->inputPoint(i));
    for (unsigned int i = 0; i < schema->outputs; i++) c.addInput(schema->outputPoint(i));
}

// A `DecorateSchema` is a schema surrounded by a dashed rectangle with a label on the top left.
// The rectangle is placed at half the margin parameter.
struct DecorateSchema : Schema {
    friend Schema *makeDecorateSchema(Schema *s1, double margin, const string &text);

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

private:
    DecorateSchema(Schema *s1, double margin, string text);

    Schema *schema;
    double fMargin;
    string text;
    vector<Point> inputPoints;
    vector<Point> outputPoints;
};

Schema *makeDecorateSchema(Schema *s, double margin, const string &text) { return new DecorateSchema(s, margin, text); }

// A DecorateSchema is a schema surrounded by a dashed rectangle with a label on the top left.
// The rectangle is placed at half the margin parameter.
// The constructor is made private to enforce the usage of `makeDecorateSchema`
DecorateSchema::DecorateSchema(Schema *s, double margin, string text)
    : Schema(s->inputs, s->outputs, s->width + 2 * margin, s->height + 2 * margin), schema(s), fMargin(margin), text(std::move(text)) {
    for (unsigned int i = 0; i < inputs; i++) inputPoints.emplace_back(0, 0);
    for (unsigned int i = 0; i < outputs; i++) outputPoints.emplace_back(0, 0);
}

void DecorateSchema::placeImpl() {
    schema->place(x + fMargin, y + fMargin, orientation);

    const double m = orientation == kRightLeft ? -fMargin : fMargin;
    for (unsigned int i = 0; i < inputs; i++) {
        const auto p = schema->inputPoint(i);
        inputPoints[i] = {p.x - m, p.y}; // todo inline with `= p - {m, 0}` and vectorize
    }
    for (unsigned int i = 0; i < outputs; i++) {
        const auto p = schema->outputPoint(i);
        outputPoints[i] = {p.x + m, p.y}; // todo inline with `= p + {m, 0}` and vectorize
    }
}

Point DecorateSchema::inputPoint(unsigned int i) const { return inputPoints[i]; }
Point DecorateSchema::outputPoint(unsigned int i) const { return outputPoints[i]; }

void DecorateSchema::draw(Device &device) {
    schema->draw(device);

    // Surrounding frame
    const double x0 = x + fMargin / 2; // left
    const double y0 = y + fMargin / 2; // top
    const double x1 = x + width - fMargin / 2; // right
    const double y1 = y + height - fMargin / 2; // bottom
    const double tl = x + fMargin; // left of text zone

    device.dasharray(x0, y0, x0, y1); // left line
    device.dasharray(x0, y1, x1, y1); // bottom line
    device.dasharray(x1, y1, x1, y0); // right line
    device.dasharray(x0, y0, tl, y0); // top segment before text
    device.dasharray(min(tl + double(2 + text.size()) * dLetter * 0.75, x1), y0, x1, y0); // top segment after text

    device.label(tl, y0, text.c_str());
}

void DecorateSchema::collectLines(Collector &c) {
    schema->collectLines(c);
    for (unsigned int i = 0; i < inputs; i++) c.addLine({inputPoint(i), schema->inputPoint(i)});
    for (unsigned int i = 0; i < outputs; i++) c.addLine({schema->outputPoint(i), outputPoint(i)});
}

// A simple rectangular box with a text and inputs and outputs.
// The constructor is private in order to make sure `makeConnectorSchema` is used instead.
struct ConnectorSchema : Schema {
    friend Schema *makeConnectorSchema();

    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

protected:
    ConnectorSchema();

    vector<Point> inputPoints;
    vector<Point> outputPoints;
};

// Connectors are used to ensure unused inputs and outputs are drawn.
Schema *makeConnectorSchema() { return new ConnectorSchema(); }

// A connector is an invisible square for `dWire` size with 1 input and 1 output.
ConnectorSchema::ConnectorSchema() : Schema(1, 1, dWire, dWire) {
    inputPoints.emplace_back(0, 0);
    outputPoints.emplace_back(0, 0);
}

void ConnectorSchema::placeImpl() {
    const bool isLR = orientation == kLeftRight;
    for (unsigned int i = 0; i < inputs; i++) inputPoints[i] = {isLR ? Schema::x : Schema::x + width, Schema::y + height / 2.0 - dWire * ((inputs - 1) / 2.0 + i * (isLR ? -1.0 : 1.0))};
    for (unsigned int i = 0; i < outputs; i++) outputPoints[i] = {isLR ? x + width : x, y + height / 2.0 - dWire * ((outputs - 1) / 2.0 + i * (isLR ? -1.0 : 1.0))};
}

Point ConnectorSchema::inputPoint(unsigned int i) const { return inputPoints[i]; }
Point ConnectorSchema::outputPoint(unsigned int i) const { return outputPoints[i]; }

void ConnectorSchema::draw(Device &) {}

void ConnectorSchema::collectLines(Collector &c) {
    const double dx = (orientation == kLeftRight) ? dHorz : -dHorz;
    // Input wires
    for (const auto &p: inputPoints) {
        c.addLine({p, {p.x + dx, p.y}});
        c.addInput({p.x + dx, p.y});
    }
    // Output wires
    for (const auto &p: outputPoints) {
        c.addLine({{p.x - dx, p.y}, p});
        c.addOutput({p.x - dx, p.y});
    }
}

// A simple rectangular box with a text and inputs and outputs.
// The constructor is private in order to make sure `makeBlockSchema` is used instead.
struct RouteSchema : Schema {
    friend Schema *makeRouteSchema(unsigned int n, unsigned int m, const std::vector<int> &routes);
    void placeImpl() override;
    void draw(Device &) override;
    Point inputPoint(unsigned int i) const override;
    Point outputPoint(unsigned int i) const override;
    void collectLines(Collector &) override;

protected:
    RouteSchema(unsigned int inputs, unsigned int outputs, double width, double height, std::vector<int> routes);

    const string text;
    const string color;
    const string link;
    const std::vector<int> routes;  // Route description: s1,d2,s2,d2,...

    vector<Point> inputPoints;
    vector<Point> outputPoints;
};

// Build n x m cable routing
Schema *makeRouteSchema(unsigned int inputs, unsigned int outputs, const std::vector<int> &routes) {
    const double minimal = 3 * dWire;
    const double h = 2 * dVert + max(minimal, max(inputs, outputs) * dWire);
    const double w = 2 * dHorz + max(minimal, h * 0.75);
    return new RouteSchema(inputs, outputs, w, h, routes);
}

// Build a simple colored `RouteSchema` with a certain number of inputs and outputs, a text to be displayed, and an optional link.
// The length of the text as well as the number of inputs and outputs are used to compute the size of the `RouteSchema`
RouteSchema::RouteSchema(unsigned int inputs, unsigned int outputs, double width, double height, std::vector<int> routes)
    : Schema(inputs, outputs, width, height), text(""), color("#EEEEAA"), link(""), routes(std::move(routes)) {
    for (unsigned int i = 0; i < inputs; i++) inputPoints.emplace_back(Point(0, 0));
    for (unsigned int i = 0; i < outputs; i++) outputPoints.emplace_back(0, 0);
}

void RouteSchema::placeImpl() {
    const bool isLR = orientation == kLeftRight;
    for (unsigned int i = 0; i < inputs; i++) inputPoints[i] = {isLR ? x : x + width, y + height / 2.0 - dWire * ((inputs - 1) / 2.0 + i * (isLR ? -1.0 : 1.0))};
    for (unsigned int i = 0; i < outputs; i++) outputPoints[i] = {isLR ? x + width : x, y + height / 2.0 - dWire * ((outputs - 1) / 2.0 + i * (isLR ? -1.0 : 1.0))};
}

Point RouteSchema::inputPoint(unsigned int i) const { return inputPoints[i]; }
Point RouteSchema::outputPoint(unsigned int i) const { return outputPoints[i]; }

void RouteSchema::draw(Device &device) {
    static bool drawRouteFrame = false; // todo provide toggle
    if (drawRouteFrame) {
        device.rect(x + dHorz, y + dVert, width - 2 * dHorz, height - 2 * dVert, color, link);
        // device.text(x + width / 2, y + height / 2, text.c_str(), link);

        // Draw the orientation mark, a small point that indicates the first input (like integrated circuits).
        const bool isLR = orientation == kLeftRight;
        device.dot(x + (isLR ? dHorz : (width - dHorz)), y + (isLR ? dVert : (height - dVert)), orientation);

        // Input arrows
        const double dx = isLR ? dHorz : -dHorz;
        for (const auto &p: inputPoints) device.arrow(p.x + dx, p.y, 0, orientation);
    }
}

void RouteSchema::collectLines(Collector &c) {
    const double dx = orientation == kLeftRight ? dHorz : -dHorz;
    // Input wires
    for (const auto &p: inputPoints) {
        c.addLine({p, {p.x + dx, p.y}});
        c.addInput({p.x + dx, p.y});
    }
    // Output wires
    for (const auto &p: outputPoints) {
        c.addLine({{p.x - dx, p.y}, p});
        c.addOutput({p.x - dx, p.y});
    }
    // Route wires
    for (unsigned int i = 0; i < routes.size() - 1; i += 2) {
        const auto p1 = inputPoints[routes[i] - 1];
        const auto p2 = outputPoints[routes[i + 1] - 1];
        c.addLine({{p1.x + dx, p1.y}, {p2.x - dx, p2.y}});
    }
}
