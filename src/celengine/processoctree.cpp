
#include "star.h"
#include "deepskyobj.h"
#include "astrooctree.h"
#include "processoctree.h"

using namespace Eigen;

static constexpr double SQRT3 = 1.732050807568877;

static constexpr double MAX_STAR_ORBIT_RADIUS = 1;

void create5FrustumPlanes(Frustum::PlaneType *frustumPlanes, Vector3d position, Quaternionf orientation, float fovY, float aspectRatio)
{
    Vector3d planeNormals[5];
    Eigen::Matrix3f rot = orientation.toRotationMatrix();
    double h = (float) tan(fovY / 2);
    double w = h * aspectRatio;
    planeNormals[0] = Vector3d(0.0, 1.0, -h);
    planeNormals[1] = Vector3d(0.0, -1.0, -h);
    planeNormals[2] = Vector3d(1.0, 0.0, -w);
    planeNormals[3] = Vector3d(-1.0, 0.0, -w);
    planeNormals[4] = Vector3d(0.0, 0.0, -1.0);
    for (int i = 0; i < 5; i++)
    {
        planeNormals[i] = rot.cast<double>().transpose() * planeNormals[i].normalized();
        frustumPlanes[i] = Frustum::PlaneType(planeNormals[i].cast<float>(), position.cast<float>());
    }
}

void processVisibleStars(
    const OctreeNode *node,
    StarProcesor& procesor,
    const Vector3d& obsPosition,
    const Frustum::PlaneType *frustumPlanes,
    float limitingFactor,
    OctreeProcStats *stats)
{
    size_t h = 0;
    if (stats != nullptr)
    {
        h = stats->height + 1;
        stats->nodes++;
    }

    if (!node->isInFrustum(frustumPlanes))
        return;

    // Compute the distance to node; this is equal to the distance to
    // the cellCenterPos of the node minus the boundingRadius of the node, scale * SQRT3.
    float minDistance = (obsPosition - node->getCenter()).norm() - node->getScale() * SQRT3;

    // Process the objects in this node
    float dimmest     = minDistance > 0 ? astro::appToAbsMag(limitingFactor, minDistance) : 1000;

    for (const auto &objit : node->getObjects())
    {
        Star *obj = static_cast<Star*>(objit.second);
        if (stats != nullptr)
            stats->objects++;
        if (obj->getAbsoluteMagnitude() < dimmest)
        {
            double distance = (obsPosition - obj->getPosition().cast<double>()).norm();
            float appMag = astro::absToAppMag(obj->getAbsoluteMagnitude(), (float)distance);

            if (appMag < limitingFactor || (distance < MAX_STAR_ORBIT_RADIUS && obj->getOrbit()))
                procesor.process(obj, distance, appMag);
        }
    }

    // See if any of the objects in child nodes are potentially included
    // that we need to recurse deeper.
    if (minDistance <= 0 || astro::absToAppMag(node->getFaintest(), minDistance) <= limitingFactor)
    {
        // Recurse into the child nodes
        for (const auto &child : node->getChildren())
        {
            if (child == nullptr)
                continue;
            processVisibleStars(child,
                                procesor,
                                obsPosition,
                                frustumPlanes,
                                limitingFactor,
                                stats);
            if (stats != nullptr && stats->height > h)
                h = stats->height;
        }
        if (stats != nullptr)
            stats->height = h;
    }
}

void processVisibleStars(
    const OctreeNode *node,
    StarProcesor& procesor,
    Vector3d position,
    Quaternionf orientation,
    float fovY,
    float aspectRatio,
    float limitingFactor,
    OctreeProcStats *stats)
{
    Frustum::PlaneType fp[5];
    create5FrustumPlanes(fp, position, orientation, fovY, aspectRatio);
    processVisibleStars(node, procesor, position, fp, limitingFactor, stats);
}

void processVisibleDsos(
    const OctreeNode *node,
    DsoProcesor& procesor,
    const Eigen::Vector3d& obsPosition,
    const Frustum::PlaneType *frustumPlanes,
    float limitingFactor,
    OctreeProcStats *stats)
{
    size_t h = 0;
    if (stats != nullptr)
    {
        stats->nodes++;
        h = stats->height + 1;
    }
    // See if this node lies within the view frustum

    // Test the cubic octree node against each one of the five
    // planes that define the infinite view frustum.
    if (node->isInFrustum(frustumPlanes))
        return;

    // Compute the distance to node; this is equal to the distance to
    // the cellCenterPos of the node minus the boundingRadius of the node, scale * SQRT3.
    double minDistance = (obsPosition - node->getCenter()).norm() - node->getScale() * SQRT3;

    // Process the objects in this node
    double dimmest = minDistance > 0.0 ? astro::appToAbsMag((double) limitingFactor, minDistance) : 1000.0;

    for (const auto &objit : node->getObjects())
    {
        DeepSkyObject *obj = static_cast<DeepSkyObject*>(objit.second);

        if (stats != nullptr)
            stats->objects++;
        float absMag = obj->getAbsoluteMagnitude();
        if (absMag < dimmest)
        {
            double distance = (obsPosition - obj->getPosition().cast<double>()).norm() - obj->getBoundingSphereRadius();
            float appMag = (float) ((distance >= 32.6167) ? astro::absToAppMag((double) absMag, distance) : absMag);

            if (appMag < limitingFactor)
                procesor.process(obj, distance, absMag);
        }
    }

    // See if any of the objects in child nodes are potentially included
    // that we need to recurse deeper.
    if (minDistance <= 0.0 || astro::absToAppMag((double) node->getFaintest(), minDistance) <= limitingFactor)
    {
        // Recurse into the child nodes
        for (const auto &child : node->getChildren())
        {
            if (child == nullptr)
                continue;
            processVisibleDsos(
                child,
                procesor,
                obsPosition,
                frustumPlanes,
                limitingFactor,
                stats);
            if (stats != nullptr && h < stats->height)
                h = stats->height;
        }
        if (stats != nullptr)
            stats->height = h;
    }
}

void processVisibleDsos(
    const OctreeNode *node,
    DsoProcesor& procesor,
    Vector3d position,
    Quaternionf orientation,
    float fovY,
    float aspectRatio,
    float limitingFactor,
    OctreeProcStats *stats)
{
    Frustum::PlaneType fp[5];
    create5FrustumPlanes(fp, position, orientation, fovY, aspectRatio);
    processVisibleDsos(node, procesor, position, fp, limitingFactor, stats);
}

void processCloseStars(
    const OctreeNode *node,
    StarProcesor& procesor,
    const Vector3d& obsPosition,
    double boundingRadius)
{
    // Compute the distance to node; this is equal to the distance to
    // the cellCenterPos of the node minus the boundingRadius of the node, scale * SQRT3.
    double nodeDistance    = (obsPosition - node->getCenter()).norm() - node->getScale() * SQRT3;

    if (nodeDistance > boundingRadius)
        return;

    // At this point, we've determined that the cellCenterPos of the node is
    // close enough that we must check individual objects for proximity.

    // Compute distance squared to avoid having to sqrt for distance
    // comparison.
    double radiusSquared = boundingRadius * boundingRadius;

    // Check all the objects in the node.
    for (const auto &objit : node->getObjects())
    {
        Star *obj = static_cast<Star*>(objit.second);

        if ((obsPosition - obj->getPosition().cast<double>()).squaredNorm() < radiusSquared)
        {
            double distance = (obsPosition - obj->getPosition().cast<double>()).norm();
            float appMag = astro::absToAppMag(obj->getAbsoluteMagnitude(), (float)distance);

            procesor.process(obj, distance, appMag);
        }
    }

    // Recurse into the child nodes
    for (const auto &child : node->getChildren())
    {
        if (child == nullptr)
            continue;
        processCloseStars(
            child,
            procesor,
            obsPosition,
            boundingRadius);
    }
}

void processCloseDsos(
    const OctreeNode *node,
    DsoProcesor& procesor,
    const Vector3d& obsPosition,
    double boundingRadius)
{
    // Compute the distance to node; this is equal to the distance to
    // the cellCenterPos of the node minus the boundingRadius of the node, scale * SQRT3.
    double nodeDistance  = (obsPosition - node->getCenter()).norm() - node->getScale() * SQRT3;    //

    if (nodeDistance > boundingRadius)
        return;

    // At this point, we've determined that the cellCenterPos of the node is
    // close enough that we must check individual objects for proximity.

    // Compute distance squared to avoid having to sqrt for distance
    // comparison.
    double radiusSquared = boundingRadius * boundingRadius;    //

    // Check all the objects in the node.
    for (const auto &objit : node->getObjects())
    {
        DeepSkyObject *obj = static_cast<DeepSkyObject*>(objit.second);

        if ((obsPosition - obj->getPosition().cast<double>()).squaredNorm() < radiusSquared)    //
        {
            float  absMag = obj->getAbsoluteMagnitude();
            double distance = (obsPosition - obj->getPosition().cast<double>()).norm() - obj->getBoundingSphereRadius();

            procesor.process(obj, distance, absMag);
        }
    }

    // Recurse into the child nodes
    for (const auto &child : node->getChildren())
    {
        if (child == nullptr)
            continue;
        processCloseDsos(
            child,
            procesor,
            obsPosition,
            boundingRadius);
    }
}
