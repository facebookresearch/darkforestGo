--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

local ParallelCriterion, parent = torch.class('nn.ParallelCriterion2', 'nn.Criterion')

function ParallelCriterion:__init(rep_count)
   parent.__init(self)
   self.criterions = {}
   self.weights = {}
   self.gradInput = {}
   -- If rep_count == 2, and #criterions = 10, we expect 5 targets.
   -- The 10 criterions will receive the following targets in order: t1, t2, t3, t4, t5,    t1, t2, t3, t3, t5
   self.rep_count = rep_count or 1
end

function ParallelCriterion:add(criterion, weight)
   weight = weight or 1
   table.insert(self.criterions, criterion)
   table.insert(self.weights, weight)
   return self
end

function ParallelCriterion:updateOutput(input, target)
   self.output = 0
   assert(#self.criterions == target:size(2) * self.rep_count,
          string.format("ParallelCriterion2: #criterions [%d] != #target [%d] * #repcount [%d]", #self.criterions, target:size(2), self.rep_count))
   for i,criterion in ipairs(self.criterions) do
      -- Target size is nbatch x #targets, which is more suitable for torchnet setting.
      local target_idx = (i - 1) % target:size(2) + 1
      local target = target:select(2, target_idx)
      self.output = self.output + self.weights[i]*criterion:updateOutput(input[i],target)
   end
   return self.output
end

function ParallelCriterion:updateGradInput(input, target)
   self.gradInput = nn.utils.recursiveResizeAs(self.gradInput, input)
   nn.utils.recursiveFill(self.gradInput, 0)
   for i,criterion in ipairs(self.criterions) do
      local target_idx = (i - 1) % target:size(2) + 1
      local target = target:select(2, target_idx)
      nn.utils.recursiveAdd(self.gradInput[i], self.weights[i], criterion:updateGradInput(input[i], target))
   end
   return self.gradInput
end

function ParallelCriterion:type(type)
   self.gradInput = {}
   return parent.type(self, type)
end

