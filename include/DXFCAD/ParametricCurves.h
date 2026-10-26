#pragma once
#include <DXFCAD/DXFDocument.h>

namespace DXFCAD {
std::string affineTransformedCurve(const DXFEntity& entity,const BlockPart& transform);

class EllipseCurve final : public DXFEntity {
public:
    explicit EllipseCurve(DXFRecord record);
    Vector3D center() const { return center_; }
    Vector3D majorAxis() const { return major_; }
    Vector3D minorAxis() const { return minor_; }
    Vector3D atParameter(double parameter) const;
    double radiusOfCurvature(double parameter) const;
    double startParameter() const { return start_; }
    double endParameter() const { return end_; }
    EntityGeometry geometry() const override;
    std::string translated(const Vector3D& offset) const override;
    std::vector<DXFSnapPoint> snapPoints() const override;
private:
    Vector3D center_,major_,minor_;
    double start_,end_;
};
class SplineCurve final : public DXFEntity {
public:
    explicit SplineCurve(DXFRecord record);
    int degree() const { return degree_; }
    bool periodic() const { return periodic_; }
    const std::vector<Vector3D>& controlPoints() const { return points_; }
    const std::vector<double>& knots() const { return knots_; }
    const std::vector<double>& weights() const { return weights_; }
    double startParameter() const;
    double endParameter() const;
    Vector3D atParameter(double parameter) const;
    EntityGeometry geometry() const override;
    std::string translated(const Vector3D& offset) const override;
    std::vector<DXFSnapPoint> snapPoints() const override;
private:
    int degree_;
    bool periodic_;
    std::vector<Vector3D> points_;
    std::vector<double> knots_,weights_;
};
}
