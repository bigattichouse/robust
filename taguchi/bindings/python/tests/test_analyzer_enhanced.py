"""
Unit tests for enhanced analyzer functionality.
"""

import csv
import tempfile
import pytest
from unittest.mock import Mock, patch

from taguchi.config import TaguchiConfig
from taguchi.core_enhanced import Taguchi
from taguchi.experiment_enhanced import Experiment
from taguchi.analyzer_enhanced import Analyzer
from taguchi.errors import TaguchiError, ValidationError


class TestAnalyzerEnhanced:
    """Test the enhanced Analyzer class."""
    
    @pytest.fixture
    def mock_taguchi(self):
        """Create a mock Taguchi instance for testing."""
        mock = Mock(spec=Taguchi)
        mock.effects.return_value = """
        temp    0.050   L1=1.020, L2=1.070, L3=1.030
        pressure 0.030   L1=1.040, L2=1.010
        """
        return mock
    
    @pytest.fixture
    def mock_experiment(self):
        """Create a mock Experiment instance for testing."""
        mock = Mock(spec=Experiment)
        mock.generate.return_value = [
            {"run_id": 1, "factors": {"temp": "low", "pressure": "low"}},
            {"run_id": 2, "factors": {"temp": "medium", "pressure": "high"}},
            {"run_id": 3, "factors": {"temp": "high", "pressure": "low"}},
        ]
        mock.factors = {
            "temp": ["low", "medium", "high"],
            "pressure": ["low", "high"]
        }
        mock.get_tgu_path.return_value = "/tmp/test.tgu"
        return mock
    
    def test_init_default(self, mock_experiment, mock_taguchi):
        """Test default initialization."""
        with patch('taguchi.analyzer_enhanced.Taguchi', return_value=mock_taguchi):
            analyzer = Analyzer(mock_experiment)
            
            assert analyzer._experiment is mock_experiment
            assert analyzer._metric_name == "response"
            assert analyzer._taguchi is mock_taguchi
    
    def test_init_with_dependency_injection(self, mock_experiment, mock_taguchi):
        """Test initialization with dependency injection."""
        analyzer = Analyzer(mock_experiment, metric_name="accuracy", taguchi=mock_taguchi)
        
        assert analyzer._experiment is mock_experiment
        assert analyzer._metric_name == "accuracy"
        assert analyzer._taguchi is mock_taguchi
    
    def test_init_with_config(self, mock_experiment):
        """Test initialization with configuration."""
        config = TaguchiConfig(debug_mode=True)
        
        with patch('taguchi.analyzer_enhanced.Taguchi') as mock_taguchi_class:
            analyzer = Analyzer(mock_experiment, config=config)
            
            # Should pass config to Taguchi constructor
            mock_taguchi_class.assert_called_once_with(config=config)
    
    def test_validate_success(self, mock_experiment, mock_taguchi):
        """Test successful analyzer validation."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        
        errors = analyzer.validate()
        assert errors == []
        assert analyzer.is_valid()
    
    def test_validate_empty_metric_name(self, mock_experiment, mock_taguchi):
        """Test validation with empty metric name."""
        analyzer = Analyzer(mock_experiment, metric_name="", taguchi=mock_taguchi)
        
        errors = analyzer.validate()
        assert "Metric name cannot be empty" in errors[0]
    
    def test_validate_invalid_metric_name(self, mock_experiment, mock_taguchi):
        """Test validation with invalid metric name."""
        analyzer = Analyzer(mock_experiment, metric_name="metric@name!", taguchi=mock_taguchi)
        
        errors = analyzer.validate()
        assert "invalid characters" in errors[0]
    
    def test_validate_missing_results(self, mock_experiment, mock_taguchi):
        """Test validation with missing results."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        # Missing results for runs 2 and 3
        
        errors = analyzer.validate()
        assert any("Missing results for runs: [2, 3]" in error for error in errors)
    
    def test_validate_extra_results(self, mock_experiment, mock_taguchi):
        """Test validation with extra results."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        analyzer.add_result(99, 0.80)  # Non-existent run
        
        errors = analyzer.validate()
        assert any("Results provided for non-existent runs: [99]" in error for error in errors)
    
    def test_validate_invalid_result_values(self, mock_experiment, mock_taguchi):
        """Test validation with invalid result values."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer._results[1] = "invalid"  # Bypass add_result validation
        analyzer._results[2] = float('inf')
        
        errors = analyzer.validate()
        assert any("not a valid number" in error for error in errors)
        assert any("out of reasonable bounds" in error for error in errors)
    
    def test_validate_and_raise(self, mock_experiment, mock_taguchi):
        """Test validate_and_raise method."""
        analyzer = Analyzer(mock_experiment, metric_name="", taguchi=mock_taguchi)
        
        # Should raise with invalid metric name
        with pytest.raises(ValidationError):
            analyzer.validate_and_raise()
        
        # Should not raise with valid setup
        analyzer._metric_name = "valid_metric"
        analyzer.validate_and_raise()  # Should not raise
    
    def test_check_completeness(self, mock_experiment, mock_taguchi):
        """Test completeness checking."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        # Missing result for run 3
        
        completeness = analyzer.check_completeness()
        
        assert completeness["total_runs"] == 3
        assert completeness["results_provided"] == 2
        assert completeness["missing_results"] == [3]
        assert completeness["extra_results"] == []
        assert completeness["is_complete"] is False
        assert abs(completeness["completion_percentage"] - 66.67) < 0.1
    
    def test_add_result_success(self, mock_experiment, mock_taguchi):
        """Test successful result addition."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        result = analyzer.add_result(1, 0.95)
        
        # Should return self for chaining
        assert result is analyzer
        assert analyzer._results[1] == 0.95
    
    def test_add_result_invalid_run_id(self, mock_experiment, mock_taguchi):
        """Test adding result for invalid run ID."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        with pytest.raises(ValidationError) as exc_info:
            analyzer.add_result(99, 0.95)
        
        assert "Run ID 99 not found in experiment" in str(exc_info.value)
    
    def test_add_result_invalid_value(self, mock_experiment, mock_taguchi):
        """Test adding invalid result value."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        # Non-numeric value
        with pytest.raises(ValidationError) as exc_info:
            analyzer.add_result(1, "invalid")
        
        assert "not a valid number" in str(exc_info.value)
        
        # Out of bounds value
        with pytest.raises(ValidationError) as exc_info:
            analyzer.add_result(1, 1e20)
        
        assert "out of reasonable bounds" in str(exc_info.value)
    
    def test_add_results_from_dict(self, mock_experiment, mock_taguchi):
        """Test adding results from dictionary."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        results = {1: 0.95, 2: 0.87, 3: 0.92}
        result = analyzer.add_results_from_dict(results)
        
        assert result is analyzer
        assert analyzer._results == results
    
    def test_add_results_from_list(self, mock_experiment, mock_taguchi):
        """Test adding results from list."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        results = [0.95, 0.87, 0.92]
        result = analyzer.add_results_from_list(results)
        
        assert result is analyzer
        assert analyzer._results[1] == 0.95
        assert analyzer._results[2] == 0.87
        assert analyzer._results[3] == 0.92
    
    def test_add_results_from_list_wrong_length(self, mock_experiment, mock_taguchi):
        """Test adding results from list with wrong length."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        results = [0.95, 0.87]  # Missing one result
        
        with pytest.raises(ValidationError) as exc_info:
            analyzer.add_results_from_list(results)
        
        assert "does not match number of experimental runs" in str(exc_info.value)
    
    def test_remove_result(self, mock_experiment, mock_taguchi):
        """Test result removal."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        
        result = analyzer.remove_result(1)
        
        assert result is analyzer
        assert 1 not in analyzer._results
        assert 2 in analyzer._results
    
    def test_remove_nonexistent_result(self, mock_experiment, mock_taguchi):
        """Test removing non-existent result."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        with pytest.raises(TaguchiError, match="No result found for run 1"):
            analyzer.remove_result(1)
    
    def test_clear_results(self, mock_experiment, mock_taguchi):
        """Test clearing all results."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        
        result = analyzer.clear_results()
        
        assert result is analyzer
        assert analyzer._results == {}
    
    def test_main_effects_success(self, mock_experiment, mock_taguchi):
        """Test successful main effects calculation."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        
        effects = analyzer.main_effects()
        
        assert len(effects) == 2
        
        temp_effect = next(e for e in effects if e["factor"] == "temp")
        assert temp_effect["range"] == 0.050
        assert len(temp_effect["level_means"]) == 3
        
        pressure_effect = next(e for e in effects if e["factor"] == "pressure")
        assert pressure_effect["range"] == 0.030
        assert len(pressure_effect["level_means"]) == 2
    
    def test_main_effects_no_results(self, mock_experiment, mock_taguchi):
        """Test main effects with no results."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        with pytest.raises(TaguchiError, match="No results added"):
            analyzer.main_effects()
    
    def test_main_effects_no_parseable_output(self, mock_experiment, mock_taguchi):
        """Test main effects with unparseable output."""
        mock_taguchi.effects.return_value = "Invalid output format"
        
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        
        with pytest.raises(TaguchiError) as exc_info:
            analyzer.main_effects()
        
        assert "no parseable output" in str(exc_info.value)
        assert "raw_output" in exc_info.value.diagnostic_info
    
    def test_recommend_optimal_higher_is_better(self, mock_experiment, mock_taguchi):
        """Test optimal recommendation with higher is better."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        
        optimal = analyzer.recommend_optimal(higher_is_better=True)
        
        # Based on mock effects: temp L2 (medium) has highest value 1.070
        # pressure L1 (low) has highest value 1.040
        assert optimal["temp"] == "medium"
        assert optimal["pressure"] == "low"
    
    def test_recommend_optimal_lower_is_better(self, mock_experiment, mock_taguchi):
        """Test optimal recommendation with lower is better."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        
        optimal = analyzer.recommend_optimal(higher_is_better=False)
        
        # Based on mock effects: temp L1 (low) has lowest value 1.020
        # pressure L2 (high) has lowest value 1.010
        assert optimal["temp"] == "low"
        assert optimal["pressure"] == "high"
    
    def test_get_factor_rankings(self, mock_experiment, mock_taguchi):
        """Test factor ranking by effect size."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        
        rankings = analyzer.get_factor_rankings()
        
        assert len(rankings) == 2
        
        # Should be sorted by range (descending)
        assert rankings[0]["factor"] == "temp"
        assert rankings[0]["range"] == 0.050
        assert rankings[0]["relative_importance"] == 1.0  # Highest effect
        
        assert rankings[1]["factor"] == "pressure"
        assert rankings[1]["range"] == 0.030
        assert rankings[1]["relative_importance"] == 0.6  # 0.030/0.050
    
    def test_get_significant_factors(self, mock_experiment, mock_taguchi):
        """Test significant factor identification."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        
        # With threshold 0.5, only temp (range=0.050) should be significant
        # pressure (range=0.030) is 0.030/0.050 = 0.6 * max_range > 0.5
        significant = analyzer.get_significant_factors(threshold=0.5)
        assert "temp" in significant
        assert "pressure" in significant  # 0.6 > 0.5
        
        # With threshold 0.7, only temp should be significant
        significant = analyzer.get_significant_factors(threshold=0.7)
        assert "temp" in significant
        assert "pressure" not in significant  # 0.6 < 0.7
    
    def test_predict_response(self, mock_experiment, mock_taguchi):
        """Test response prediction."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 1.0)  # Overall mean will be (1.0 + 1.1 + 0.9) / 3 = 1.0
        analyzer.add_result(2, 1.1)
        analyzer.add_result(3, 0.9)
        
        prediction = analyzer.predict_response({
            "temp": "medium",    # L2 = 1.070, effect = 1.070 - 1.0 = 0.070
            "pressure": "low"    # L1 = 1.040, effect = 1.040 - 1.0 = 0.040
        })
        
        expected = 1.0 + 0.070 + 0.040  # Overall mean + temp effect + pressure effect
        assert prediction["predicted_response"] == expected
        assert prediction["contributions"]["overall_mean"] == 1.0
        assert prediction["contributions"]["temp"] == 0.070
        assert prediction["contributions"]["pressure"] == 0.040
        assert prediction["factor_settings"] == {"temp": "medium", "pressure": "low"}
    
    def test_predict_response_invalid_level(self, mock_experiment, mock_taguchi):
        """Test response prediction with invalid level."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 1.0)
        analyzer.add_result(2, 1.1)
        analyzer.add_result(3, 0.9)
        
        prediction = analyzer.predict_response({
            "temp": "invalid_level",
            "pressure": "low"
        })
        
        # Should include error message for invalid level
        assert "Level 'invalid_level' not found" in prediction["contributions"]["temp"]
        assert prediction["contributions"]["pressure"] == 0.040  # Still valid
    
    def test_summary(self, mock_experiment, mock_taguchi):
        """Test summary generation."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 1.0)
        analyzer.add_result(2, 1.1)
        analyzer.add_result(3, 0.9)
        
        summary = analyzer.summary(higher_is_better=True)
        
        assert "Taguchi Experiment Analysis" in summary
        assert "Data completeness: 100.0%" in summary
        assert "Main Effects" in summary
        assert "temp" in summary
        assert "pressure" in summary
        assert "Recommended Optimal Settings" in summary
        assert "higher is better" in summary
    
    def test_summary_incomplete_data(self, mock_experiment, mock_taguchi):
        """Test summary with incomplete data."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 1.0)
        analyzer.add_result(2, 1.1)
        # Missing result for run 3
        
        summary = analyzer.summary()
        
        assert "Data completeness: 66.7%" in summary
        assert "WARNING: Missing results for runs: [3]" in summary
    
    def test_export_results(self, mock_experiment, mock_taguchi):
        """Test results export to CSV."""
        analyzer = Analyzer(mock_experiment, metric_name="accuracy", taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        
        with tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False) as f:
            csv_path = f.name
        
        try:
            analyzer.export_results(csv_path)
            
            # Read back and verify
            with open(csv_path, 'r') as f:
                reader = csv.DictReader(f)
                rows = list(reader)
            
            assert len(rows) == 2
            assert rows[0]["run_id"] == "1"
            assert rows[0]["accuracy"] == "0.95"
            assert rows[1]["run_id"] == "2"
            assert rows[1]["accuracy"] == "0.87"
        
        finally:
            import os
            os.unlink(csv_path)
    
    def test_import_results(self, mock_experiment, mock_taguchi):
        """Test results import from CSV."""
        analyzer = Analyzer(mock_experiment, metric_name="accuracy", taguchi=mock_taguchi)
        
        # Create test CSV
        csv_data = [
            ["run_id", "accuracy"],
            ["1", "0.95"],
            ["2", "0.87"],
            ["3", "0.92"]
        ]
        
        with tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False) as f:
            writer = csv.writer(f)
            writer.writerows(csv_data)
            csv_path = f.name
        
        try:
            result = analyzer.import_results(csv_path)
            
            assert result is analyzer
            assert analyzer._results[1] == 0.95
            assert analyzer._results[2] == 0.87
            assert analyzer._results[3] == 0.92
        
        finally:
            import os
            os.unlink(csv_path)
    
    def test_import_results_missing_columns(self, mock_experiment, mock_taguchi):
        """Test import with missing columns."""
        analyzer = Analyzer(mock_experiment, metric_name="accuracy", taguchi=mock_taguchi)
        
        # CSV without accuracy column
        csv_data = [
            ["run_id", "other_metric"],
            ["1", "0.95"],
            ["2", "0.87"]
        ]
        
        with tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False) as f:
            writer = csv.writer(f)
            writer.writerows(csv_data)
            csv_path = f.name
        
        try:
            with pytest.raises(ValidationError) as exc_info:
                analyzer.import_results(csv_path)
            
            assert "Metric column 'accuracy' not found" in str(exc_info.value)
        
        finally:
            import os
            os.unlink(csv_path)
    
    def test_string_representations(self, mock_experiment, mock_taguchi):
        """Test string and repr methods."""
        analyzer = Analyzer(mock_experiment, metric_name="accuracy", taguchi=mock_taguchi)
        
        # Empty analyzer
        analyzer_str = str(analyzer)
        assert "Taguchi Analyzer for 'accuracy'" in analyzer_str
        assert "0/3 (0.0%)" in analyzer_str
        
        # With some results
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        
        analyzer_str = str(analyzer)
        assert "2/3 (66.7%)" in analyzer_str
        
        analyzer_repr = repr(analyzer)
        assert "Analyzer(metric='accuracy'" in analyzer_repr
        assert "results=2" in analyzer_repr
        assert "analyzed=False" in analyzer_repr  # main_effects not called yet


class TestAnalyzerIntegration:
    """Integration tests for enhanced analyzer functionality."""
    
    def test_validation_caching(self, mock_experiment, mock_taguchi):
        """Test that validation results are cached appropriately."""
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        
        # First validation
        errors1 = analyzer.validate()
        
        # Second validation should use cache (same object reference)
        errors2 = analyzer.validate()
        assert errors1 is errors2
        
        # Adding result should invalidate cache
        analyzer.add_result(1, 0.95)
        errors3 = analyzer.validate()
        assert errors3 is not errors1
    
    def test_configuration_propagation(self, mock_experiment):
        """Test that configuration propagates correctly."""
        config = TaguchiConfig(debug_mode=True, cli_timeout=120)
        
        with patch('taguchi.analyzer_enhanced.Taguchi') as mock_taguchi_class:
            mock_instance = Mock()
            mock_taguchi_class.return_value = mock_instance
            
            analyzer = Analyzer(mock_experiment, config=config)
            
            # Should create Taguchi with config
            mock_taguchi_class.assert_called_once_with(config=config)
            assert analyzer._taguchi is mock_instance
    
    def test_backward_compatibility(self, mock_experiment):
        """Test backward compatibility with original API."""
        with patch('taguchi.analyzer_enhanced.Taguchi') as mock_taguchi_class:
            mock_instance = Mock()
            mock_taguchi_class.return_value = mock_instance
            
            # Original constructor pattern should still work
            analyzer = Analyzer(mock_experiment, "response")
            
            assert analyzer._metric_name == "response"
            assert analyzer._taguchi is mock_instance
    
    def test_error_propagation(self, mock_experiment, mock_taguchi):
        """Test that errors from Taguchi are properly propagated."""
        mock_taguchi.effects.side_effect = TaguchiError("CLI error")
        
        analyzer = Analyzer(mock_experiment, taguchi=mock_taguchi)
        analyzer.add_result(1, 0.95)
        analyzer.add_result(2, 0.87)
        analyzer.add_result(3, 0.92)
        
        with pytest.raises(TaguchiError, match="CLI error"):
            analyzer.main_effects()