#include "frontend/TagResolver.h"

namespace asn1::frontend {

static void resolveNodeTags(const AsnNodePtr& node, AsnNode::TaggingEnvironment env) {
    if (!node) return;

    AsnNode::TaggingEnvironment currentEnv = env;
    if (node->type == NodeType::MODULE) {
        currentEnv = node->tagging_environment;
    }

    // If this node itself is a type that has an explicit tag without a mode,
    // set the mode based on the environment.
    if (node->tag.has_value() && node->tag->mode == AsnNode::TaggingMode::UNSPECIFIED) {
        if (currentEnv == AsnNode::TaggingEnvironment::IMPLICIT || currentEnv == AsnNode::TaggingEnvironment::AUTOMATIC) {
            node->tag->mode = AsnNode::TaggingMode::IMPLICIT;
        } else { // EXPLICIT or UNSPECIFIED environment defaults to EXPLICIT
            node->tag->mode = AsnNode::TaggingMode::EXPLICIT;
        }
    }

    if (node->type == NodeType::SEQUENCE || node->type == NodeType::SET || node->type == NodeType::CHOICE) {
        if (currentEnv == AsnNode::TaggingEnvironment::AUTOMATIC) {
            int tagCounter = 0;
            for (const auto& member : node->children) {
                if (member->type != NodeType::ASSIGNMENT) {
                    // Skip non-member nodes like extension markers
                    continue;
                }

                auto typeNode = member->getChild(0);
                if (!typeNode) continue;

                // If the member's type doesn't already have an explicit tag, assign one.
                if (!typeNode->tag.has_value()) {
                    AsnNode::Tag autoTag;
                    autoTag.tag_class = AsnNode::TagClass::CONTEXT_SPECIFIC;
                    autoTag.tag_number = tagCounter;
                    // AUTOMATIC TAGS implies IMPLICIT tagging for the automatically assigned tags.
                    autoTag.mode = AsnNode::TaggingMode::IMPLICIT;
                    typeNode->tag = autoTag;
                }
                tagCounter++;
            }
        }
    }

    // Recurse on all children.
    for (const auto& child : node->children) {
        resolveNodeTags(child, currentEnv);
    }
}

void TagResolver::resolve(const AsnNodePtr& module) {
    if (!module || module->type != NodeType::MODULE) {
        return;
    }
    resolveNodeTags(module, module->tagging_environment);
}

} // namespace asn1::frontend